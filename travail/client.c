#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>

#include "msg_struct.h"
#include "common.h"
#define INBOX_DIR ".resa1/inbox"

void die(int ret_value, const char *msg)
{
    if (ret_value < 0) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

static void trim_newline(char *str) {
    char *nl = strchr(str, '\n');
    if (nl != NULL) {
        *nl = '\0';
    }
}

int read_on_socket(int sock, void *buffer, size_t size) {
    int read_bytes = 0;
    int to_read = size;
    int ret_val;
    if (to_read == 0){
        ret_val = 0;
    }
    while(read_bytes != to_read) {
        ret_val = read(sock, (char*)(buffer) + read_bytes, to_read - read_bytes);
        if(ret_val == 0) {
            printf("client disconnected\n");
            return ret_val;
        }
        if (ret_val < 0) {
            perror("reading msg header\n");
            return -1;
        }
        read_bytes += ret_val;
    }
    return ret_val;
}

int send_on_socket(int sock, void *buffer, size_t size){
    size_t written_bytes = 0;
    int ret_value;

    while (written_bytes != size) {
        ret_value = write(sock, (char *)buffer + written_bytes, size - written_bytes);

        if (ret_value == 0) {
            printf("Disconnected\n");
            return -1;
        }

        if (ret_value < 0) {
            perror("writing");
            return -1;
        }
        written_bytes += ret_value;
    }

    return ret_value;
}

static int send_msg(int sock, enum msg_type type, const char *sender, const char *infos, const void *payload, int pld_len)
{
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = type;
    msg.pld_len = pld_len;

    if (sender != NULL && sender[0] != '\0') {
        strncpy(msg.nick_sender, sender, sizeof(msg.nick_sender) - 1);
    }
    if (infos != NULL && infos[0] != '\0') {
        strncpy(msg.infos, infos, sizeof(msg.infos) - 1);
    }

    if (send_on_socket(sock, &msg, sizeof(msg)) < 0) {
        return -1;
    }
    if (pld_len > 0 && payload != NULL) {
        if (send_on_socket(sock, (void *)payload, pld_len) < 0) {
            return -1;
        }
    }
    return 0;
}

// Pour la connexion peer to peer, il faut traiter les envoies et receptions différements ; ici co coupée = transfert échoué
static int peer_read_on_socket(int sock, void *buffer, size_t size)
{
    size_t done = 0;
    while (done < size) {
        ssize_t r = read(sock, (char *)buffer + done, size - done);
        if (r <= 0) return -1;
        done += r;
    }
    return 0;
}

static int peer_send_on_socket(int sock, const void *buffer, size_t size)
{
    size_t done = 0;
    while (done < size) {
        ssize_t w = write(sock, (const char *)buffer + done, size - done);
        if (w <= 0) return -1;
        done += w;
    }
    return 0;
}

/* Émetteur : envoie le fichier par morceaux puis attend FILE_ACK */
static void send_file(int peer_fd, const char *path, const char *name, const char *my_nick, const char *peer_nick) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        perror("fopen");
        close(peer_fd);
        return;
    }

    printf("Connecting to %s and sending the file...\n", peer_nick);

    struct message msg;
    char chunk[MSG_LEN];
    size_t n;
    int ok = 1;

    /* Req3.6 : envoi par chunk */
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        memset(&msg, 0, sizeof(msg));
        msg.type = FILE_SEND;
        msg.pld_len = (int)n;
        strncpy(msg.infos, name, sizeof(msg.infos) - 1);
        strncpy(msg.nick_sender, my_nick, sizeof(msg.nick_sender) - 1);

        if (peer_send_on_socket(peer_fd, &msg, sizeof(msg)) < 0 ||
            peer_send_on_socket(peer_fd, chunk, n) < 0) {
            ok = 0;
            break;
        }
    }
    if (ferror(fp)) ok = 0;
    fclose(fp);

    /* fin : FILE_SEND de longueur 0 */
    if (ok) {
        memset(&msg, 0, sizeof(msg));
        msg.type = FILE_SEND;
        msg.pld_len = 0;
        strncpy(msg.infos, name, sizeof(msg.infos) - 1);
        strncpy(msg.nick_sender, my_nick, sizeof(msg.nick_sender) - 1);
        if (peer_send_on_socket(peer_fd, &msg, sizeof(msg)) < 0) ok = 0;
    }

    /* Req3.5 : attente de l'ACK */
    if (ok) {
        memset(&msg, 0, sizeof(msg));
        if (peer_read_on_socket(peer_fd, &msg, sizeof(msg)) == 0 && msg.type == FILE_ACK)
            printf("%s has received the file.\n", peer_nick);
        else
            ok = 0;
    }

    if (!ok) fprintf(stderr, "File transfer to %s failed.\n", peer_nick);
    close(peer_fd);
}

// Récepteur : reçoit les morceaux, écrit dans .resa1/inbox, renvoie FILE_ACK 
static void receive_file(int peer_fd, const char *name, const char *my_nick, const char *peer_nick) {
    char path[MSG_LEN];
    char chunk[MSG_LEN];
    struct message msg;
    int ok = 1;

    if ((mkdir(".resa1", 0755) < 0 && errno != EEXIST) ||
        (mkdir(INBOX_DIR, 0755) < 0 && errno != EEXIST)) {
        perror("mkdir");
        close(peer_fd);
        return;
    }

    if (snprintf(path, sizeof(path), "%s/%s", INBOX_DIR, name) >= (int)sizeof(path)) {
        fprintf(stderr, "Chemin trop long\n");
        close(peer_fd);
        return;
    }

    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        perror("fopen");
        close(peer_fd);
        return;
    }

    printf("Receiving the file from %s...\n", peer_nick);

    while (1) {
        if (peer_read_on_socket(peer_fd, &msg, sizeof(msg)) < 0) { ok = 0; break; }
        if (msg.type != FILE_SEND) { ok = 0; break; }

        // Req 3.6
        if (msg.pld_len < 0 || msg.pld_len > MSG_LEN) {
            fprintf(stderr, "Longueur de morceau invalide : %d\n", msg.pld_len);
            ok = 0;
            break;
        }

        if (msg.pld_len == 0) break;   // fin zb

        if (peer_read_on_socket(peer_fd, chunk, msg.pld_len) < 0) { ok = 0; break; }
        if (fwrite(chunk, 1, msg.pld_len, fp) != (size_t)msg.pld_len) { ok = 0; break; }
    }

    if (fclose(fp) != 0) ok = 0;

    if (ok) {
        printf("%s saved in %s\n", name, path);

        // Req 3.5 : confirmation 
        memset(&msg, 0, sizeof(msg));
        msg.type = FILE_ACK;
        msg.pld_len = 0;
        strncpy(msg.infos, name, sizeof(msg.infos) - 1);
        strncpy(msg.nick_sender, my_nick, sizeof(msg.nick_sender) - 1);
        peer_send_on_socket(peer_fd, &msg, sizeof(msg));
    } else {
        fprintf(stderr, "File transfer from %s failed.\n", peer_nick);
        remove(path);
    }

    close(peer_fd);
}

struct client_state {
    char current_nickname[NICK_LEN];
    char pending_nickname[NICK_LEN];

    /* Transfert P2P */
    int pending_file_request;
    char file_path[MSG_LEN];
    char file_name[MSG_LEN];
    char nick_recv[NICK_LEN];
};

void echo_client(int sockfd) {
    char buff[MSG_LEN];
    struct pollfd fds[2];
    struct client_state state;
    memset(&state, 0, sizeof(state));

    int file_peer_fd = -1;

    struct message message = {0};

    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    fds[1].fd = sockfd;
    fds[1].events = POLLIN;
    // J'ai tout changer pour me conformer à poll().
    while (1) {
        
        int ret = poll(fds, 2, -1); //Tache 1.5, '-1' = attendre indef

        if (ret < 0) {
            perror("poll");
            break;
        }
        // Saisi au clavier
        if (fds[0].revents & POLLIN) {
            memset(buff, 0, MSG_LEN);
            memset(&message, 0, sizeof(message));

        if (fgets(buff, MSG_LEN, stdin) == NULL) {
            break;
        }

        // Req 3.2
        if (state.pending_file_request) {
            if (strcmp(buff, "N\n") == 0 || strcmp(buff, "n\n") == 0) {
                send_msg(sockfd, FILE_REJECT, state.current_nickname, state.nick_recv, NULL, 0);
                printf("File transfer from %s rejected.\n", state.nick_recv);
                state.pending_file_request = 0;
                state.nick_recv[0] = '\0';
                state.file_name[0] = '\0';

                continue;
            }
            else if (strcmp(buff, "Y\n") == 0 || strcmp(buff, "y\n") == 0) {
                printf("File transfer accepted.\n");
                // Socket temp
                int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
                die(listen_fd, "socket creation");
                struct sockaddr_in peer_addr = {0};
                peer_addr.sin_family = AF_INET;
                peer_addr.sin_port = htons(0);
                peer_addr.sin_addr.s_addr = INADDR_ANY;
                int ret_value = bind(listen_fd, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
                die(ret_value, "binding");
                ret_value = listen(listen_fd, 1);
                die(ret_value, "listening");
                
                // Récupérer le vrai port
                socklen_t addr_len = sizeof(peer_addr);
                ret_value = getsockname(listen_fd, (struct sockaddr *)&peer_addr, &addr_len);
                die(ret_value, "getsockname");

                int port = ntohs(peer_addr.sin_port);

                char ip_port[MSG_LEN];
                snprintf(ip_port, MSG_LEN, "127.0.0.1:%d", port);

                // Envoie au serv
                send_msg(sockfd, FILE_ACCEPT, state.current_nickname, state.nick_recv, ip_port, strlen(ip_port));
                printf("Waiting for %s on %s...\n", state.nick_recv, ip_port);
                state.pending_file_request = 0;

                file_peer_fd = accept(listen_fd, NULL, NULL);
                close(listen_fd);
                if (file_peer_fd < 0) {
                    perror("accept");
                    continue;
                }

                receive_file(file_peer_fd, state.file_name, state.current_nickname, state.nick_recv);
                file_peer_fd = -1;
                state.file_name[0] = '\0';
                state.nick_recv[0] = '\0';
                continue;
            }
            else {
                printf("Please answer Y or N.\n");
                continue;
            }
        }

        /* Req1.7 */
        if (strcmp(buff, "/quit\n") == 0) {
            printf("Deconnecting...\n");
            break;
        }

        /* Req2.1 / Req2.4 */
        if (strncmp(buff, "/nick ", 6) == 0) {
            char *nickname = buff + 6; // enleve le "/nick "
            trim_newline(nickname);

            if (strlen(nickname) == 0) {
                fprintf(stderr, "Pseudo vide interdit\n");
                continue;
            }

            if (strlen(nickname) >= NICK_LEN) {
                fprintf(stderr, "Max size for your pseudo is %d\n", NICK_LEN - 1);
                continue;
            }

            int valid = 1;

            for (size_t i = 0; i < strlen(nickname); i++) {
                if (!isalnum((unsigned char)nickname[i])) {
                    valid = 0;
                    break;
                }
            }

            if (!valid) {
                fprintf(stderr, "Pseudo invalide : lettres et chiffres uniquement\n");
                continue;
            }

            strcpy(state.pending_nickname, nickname);
            send_msg(sockfd, NICKNAME_NEW, state.current_nickname, nickname, NULL, 0);
        }

        /* Req2.5 : /who */
        else if (strcmp(buff, "/who\n") == 0) {
            send_msg(sockfd, NICKNAME_LIST, state.current_nickname, NULL, NULL, 0);
        }

        /* Req2.6 : /whois <pseudo> */
        else if (strncmp(buff, "/whois ", 7) == 0) {
            char *nickname = buff + 7;
            trim_newline(nickname);

            if (strlen(nickname) == 0) {
                fprintf(stderr, "Usage: /whois <pseudo>\n");
                continue;
            }

            send_msg(sockfd, NICKNAME_INFOS, state.current_nickname, nickname, NULL, 0);
        }

        /* Req2.7 : /msgall <message> */
        else if (strncmp(buff, "/msgall ", 8) == 0) {
            char *payload = buff + 8;
            trim_newline(payload);

            if (strlen(payload) == 0) {
                fprintf(stderr, "Usage: /msgall <message>\n");
                continue;
            }

            send_msg(sockfd, BROADCAST_SEND, state.current_nickname, NULL, payload, strlen(payload));
        }

        /* Req2.9 : /msg <pseudo> <message> */
        else if (strncmp(buff, "/msg ", 5) == 0) {
            char *nickname = buff + 5;

            char *payload = strchr(nickname, ' ');

            if (payload == NULL) {
                fprintf(stderr, "Usage: /msg <pseudo> <message>\n");
                continue;
            }

            // Espace pseudo et '\0'
            *payload = '\0';
            payload++;

            trim_newline(payload);

            if (strlen(nickname) == 0 || strlen(payload) == 0) {
                fprintf(stderr, "Usage: /msg <pseudo> <message>\n");
                continue;
            }

            send_msg(sockfd, UNICAST_SEND, state.current_nickname, nickname, payload, strlen(payload));
        }

        // Req 3.1
        else if (strncmp(buff, "/send ", 6) == 0) {
            char *user = buff + 6;
            char *file = strchr(user, ' ');
            if (file == NULL){
                fprintf(stdout, "Usage : /send <user> <dest>\n");
                continue;
            }
            *file = '\0';
            file++;
            
            trim_newline(file);

            //Gestion de toutes les erreurs possibles
            if (strlen(user) == 0 || strlen(file) == 0) {
                fprintf(stderr, "Usage : /send <user> <file>\n");
                continue;
            }
            size_t file_len = strlen(file);

            // Suppression des guillemets si y'a
            if (file_len >= 2 && file[0] == '"' && file[file_len - 1] == '"') {
                file[file_len - 1] = '\0';
                file++;
            }
            FILE *fp = fopen(file, "rb");

            if (fp == NULL) {
                perror("fopen");
                continue;
            }

            fclose(fp);

            char *filename = strrchr(file, '/');

            if (filename != NULL) {
                filename++;
            } 
            else {
                filename = file;
            }

            if (strlen(filename) == 0) {
                fprintf(stderr, "Nom de fichier invalide\n");
                continue;
            }
            if (strlen(user) >= sizeof(message.infos)) {
                fprintf(stderr, "Pseudo destinataire trop long\n");
                continue;
            }

            if (strlen(filename) >= sizeof(message.infos)) {
                fprintf(stderr, "Nom de fichier trop long\n");
                continue;
            }
            strcpy(state.file_path, file);
            strcpy(state.file_name, filename);
            strcpy(state.nick_recv, user);

            send_msg(sockfd, FILE_REQUEST, state.current_nickname, user, filename, strlen(filename));

            printf("File transfer request sent to %s for \"%s\".\n",
            user, filename);
        }

        // Req 2.11
        else {
            send_msg(sockfd, ECHO_SEND, state.current_nickname, NULL, buff, strlen(buff));
        }
    }

        if (fds[1].revents & POLLIN) {
            memset(buff, 0, MSG_LEN);
            memset(&message, 0, sizeof(message));

            read_on_socket(sockfd, &message, sizeof(message));

            if (message.pld_len < 0 || message.pld_len >= MSG_LEN) {
                fprintf(stderr, "Taille de message invalide\n");
                break;
            }

            if (message.pld_len > 0) {
                read_on_socket(sockfd, buff, message.pld_len);
                buff[message.pld_len] = '\0';
            }

            if (message.type == NICKNAME_NEW && state.pending_nickname[0] != '\0') {
                strcpy(state.current_nickname, state.pending_nickname);
                state.pending_nickname[0] = '\0';

                printf("Welcome aboard captain %s\n", state.current_nickname);
            }

            if (message.type == FILE_REQUEST) {
                if (message.pld_len == 0){
                    fprintf(stderr, "FILE_REQUEST invalide : nom de fichier manquant\n");
                    continue;
                }
                /*
                * Le serveur doit conserver le pseudo
                * de l'émetteur initial dans nick_sender.
                */
                if (strlen(message.nick_sender) >= NICK_LEN) {
                    fprintf(stderr, "Pseudo de l'emetteur trop long\n");
                    continue;
                }
                // Eviter d'écrire un fichier ailleur, genre ~/.bashrc
                if (strchr(buff, '/') != NULL ||
                    strcmp(buff, ".") == 0 || strcmp(buff, "..") == 0) {
                    fprintf(stderr, "Nom de fichier refusé : \"%s\"\n", buff);
                    continue;
                }

                strcpy(state.nick_recv, message.nick_sender);
                strcpy(state.file_name, buff);

                state.pending_file_request = 1;

                printf("%s wants you to accept the transfer " "of the file named \"%s\".\n", state.nick_recv, state.file_name);
                printf("Do you accept? [Y/N]\n");
                continue;
            }

            // User a accepté
            if (message.type == FILE_ACCEPT) {
                if (state.file_path[0] == '\0') {
                    fprintf(stderr, "FILE_ACCEPT inattendu, ignoré\n");
                    continue;
                }
                printf("%s accepted file transfer.\n", message.nick_sender);
                char ip[64];
                int port;
                
                // Séparation de l'IP et du port.
                if (sscanf(buff, "%63[^:]:%d", ip, &port) != 2) {
                    fprintf(stderr, "Adresse IP:PORT invalide\n");
                    continue;
                }

                file_peer_fd = socket(AF_INET, SOCK_STREAM, 0);
                die(file_peer_fd, "socket creation");
                struct sockaddr_in peer_addr = {0};
                peer_addr.sin_family = AF_INET;
                peer_addr.sin_port = htons(port);
                inet_aton(ip, &peer_addr.sin_addr);
                int ret_value = connect(file_peer_fd, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
                if (ret_value < 0) {
                    perror("connect");
                    close(file_peer_fd);
                    file_peer_fd = -1;
                    continue;
                }

                send_file(file_peer_fd, state.file_path, state.file_name,
                          state.current_nickname, message.nick_sender);
                file_peer_fd = -1;
                state.file_path[0] = '\0';
                state.file_name[0] = '\0';
                state.nick_recv[0] = '\0';
                continue;
            }
            if (message.type == FILE_REJECT) {
                if (message.pld_len > 0) {
                    printf("%s", buff);
                } else {
                    printf("%s cancelled file transfer.\n", message.nick_sender);
                }
                state.file_path[0] = '\0';
                state.file_name[0] = '\0';
                state.nick_recv[0] = '\0';
                continue;
            }
            if (message.pld_len > 0) {
                printf("[%s] (type=%s): %s\n",message.nick_sender, msg_type_str[message.type], buff);
            }
        }
    }
}

int handle_connect(char *server_name, char *server_port) {
	struct addrinfo hints, *result, *rp;
	int sfd;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(server_name, server_port, &hints, &result) != 0) {
		perror("getaddrinfo()");
		exit(EXIT_FAILURE);
	}
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,rp->ai_protocol);
		if (sfd == -1) {
			continue;
		}
		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
			break;
		}
		close(sfd);
	}
	if (rp == NULL) {
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}
    else {
        printf("Register your username with /nick <pseudo> please\n");
    }
	freeaddrinfo(result);
	return sfd;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_name> <server_port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int sfd = handle_connect(argv[1], argv[2]);
    echo_client(sfd);
    close(sfd);
    return EXIT_SUCCESS;
}

