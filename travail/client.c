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

#include "msg_struct.h"
#include "common.h"

void die(int ret_value, const char *msg)
{
    if (ret_value < 0) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

int recv_all(int sock, void *buffer, size_t size)
{
    size_t read_bytes = 0;
    int ret_value;

    while (read_bytes != size) {
        ret_value = read(sock, (char *)buffer + read_bytes, size - read_bytes);
        if (ret_value == 0) {
            printf("Disconnected\n");
            exit(EXIT_FAILURE);
        }
        die(ret_value, "reading");
        read_bytes += ret_value;
    }
    return ret_value;
}

int send_all(int sock, void *buffer, size_t size)
{
    size_t written_bytes = 0;
    int ret_value;

    while (written_bytes != size) {
        ret_value = write(sock, (char *)buffer + written_bytes, size - written_bytes);

        if (ret_value == 0) {
            printf("Disconnected\n");
            exit(EXIT_FAILURE);
        }

        die(ret_value, "writing");
        written_bytes += ret_value;
    }

    return ret_value;
}

void echo_client(int sockfd) {
    char buff[MSG_LEN];
    struct pollfd fds[2];
    char current_nickname[NICK_LEN] = "";
    char pending_nickname[NICK_LEN] = "";

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

        /* On remet le pseudo actuel dans le message */
        if (current_nickname[0] != '\0') {
            strcpy(message.nick_sender, current_nickname);
        }

        /* Req1.7 */
        if (strcmp(buff, "/quit\n") == 0) {
            printf("Deconnecting...\n");
            break;
        }

        /* Req2.1 / Req2.4 */
        if (strncmp(buff, "/nick ", 6) == 0) {
            char *nickname = buff + 6; // enleve le "/nick "
            char *newline = strchr(nickname, '\n');

            if (newline != NULL) {
                *newline = '\0';
            }

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

            message.type = NICKNAME_NEW;
            message.pld_len = 0;

            strcpy(message.infos, nickname);
            strcpy(pending_nickname, nickname);

            send_all(sockfd, &message, sizeof(message));
        }

        /* Req2.5 : /who */
        else if (strcmp(buff, "/who\n") == 0) {
            message.type = NICKNAME_LIST;
            message.pld_len = 0;

            send_all(sockfd, &message, sizeof(message));
        }

        /* Req2.6 : /whois <pseudo> */
        else if (strncmp(buff, "/whois ", 7) == 0) {
            char *nickname = buff + 7;
            char *newline = strchr(nickname, '\n');

            if (newline != NULL) {
                *newline = '\0';
            }

            if (strlen(nickname) == 0) {
                fprintf(stderr, "Usage: /whois <pseudo>\n");
                continue;
            }

            message.type = NICKNAME_INFOS;
            message.pld_len = 0;

            strcpy(message.infos, nickname);

            send_all(sockfd, &message, sizeof(message));
        }

        /* Req2.7 : /msgall <message> */
        else if (strncmp(buff, "/msgall ", 8) == 0) {
            char *payload = buff + 8;
            char *newline = strchr(payload, '\n');

            if (newline != NULL) {
                *newline = '\0';
            }

            if (strlen(payload) == 0) {
                fprintf(stderr, "Usage: /msgall <message>\n");
                continue;
            }

            message.type = BROADCAST_SEND;
            message.pld_len = strlen(payload);

            send_all(sockfd, &message, sizeof(message));

            if (message.pld_len > 0) {
                send_all(sockfd, payload, message.pld_len);
            }
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

            char *newline = strchr(payload, '\n');

            if (newline != NULL) {
                *newline = '\0';
            }

            if (strlen(nickname) == 0 || strlen(payload) == 0) {
                fprintf(stderr, "Usage: /msg <pseudo> <message>\n");
                continue;
            }

            message.type = UNICAST_SEND;
            message.pld_len = strlen(payload);

            strcpy(message.infos, nickname);

            send_all(sockfd, &message, sizeof(message));

            if (message.pld_len > 0) {
                send_all(sockfd, payload, message.pld_len);
            }
        }

        /* Req2.11 : echo */
        else {
            message.type = ECHO_SEND;
            message.pld_len = strlen(buff);

            send_all(sockfd, &message, sizeof(message));

            if (message.pld_len > 0) {
                send_all(sockfd, buff, message.pld_len);
            }
        }
    }

        if (fds[1].revents & POLLIN) {
            memset(buff, 0, MSG_LEN);
            memset(&message, 0, sizeof(message));

            recv_all(sockfd, &message, sizeof(message));

            if (message.pld_len < 0 || message.pld_len >= MSG_LEN) {
                fprintf(stderr, "Taille de message invalide\n");
                break;
            }

            if (message.pld_len > 0) {
                recv_all(sockfd, buff, message.pld_len);
                buff[message.pld_len] = '\0';
            }

            if (message.type == NICKNAME_NEW && pending_nickname[0] != '\0') {
                strcpy(current_nickname, pending_nickname);
                pending_nickname[0] = '\0';

                printf("Welcome aboard captain %s\n", current_nickname);
            }

            if (message.pld_len > 0) {
                printf("Received (type=%s): %s\n", msg_type_str[message.type], buff);
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

