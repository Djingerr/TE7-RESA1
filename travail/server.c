#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>

#define FDS_SIZE 128

#include "common.h"
#include "msg_struct.h"

struct client_info {
    int fd;
    struct sockaddr_storage addr;
    socklen_t addrlen;
    char nickname[NICK_LEN];
    time_t connected_at;
    struct client_info *next;
};

struct client_info *client_list_add(struct client_info **head, int fd, struct sockaddr_storage *addr, socklen_t addrlen) {
    struct client_info *node = malloc(sizeof(struct client_info));
    if(node == NULL) {
        perror("malloc");
        return NULL;
    }
    node->fd = fd;
    node->addr = *addr;
    node->addrlen = addrlen;
    node->nickname[0] = '\0';
    node->connected_at = time(NULL);
    node->next = *head;
    *head = node;
    return node;
}


void client_list_remove(struct client_info **head, int fd) {
    struct client_info *cur = *head;
    struct client_info *prev = NULL;
    while(cur != NULL)
    {
        if(cur->fd == fd) {
            if(prev == NULL)
            *head = cur->next;
            else
            prev->next = cur->next;
            free(cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

void client_list_destroy(struct client_info **head) {
    struct client_info *cur = *head;
    struct client_info *tmp;
    while(cur != NULL) {
        tmp = cur->next;
        free(cur);
        cur = tmp;
    }
    *head = NULL;
}

struct client_info* client_list_find_by_fd(struct client_info *head, int fd){
    while (head != NULL) {
        if (head->fd == fd) {
            return head;
        }
        head = head->next;
    }
    return NULL;
}

struct client_info* client_list_find_by_nickname(struct client_info *head, const char *nickname) {
    while (head != NULL) {
        if(strcmp(head->nickname, nickname) == 0) {
            return head;
        }
        head = head->next;
    }
    return NULL;
}

void die(int ret_value, const char *msg) {
    if (ret_value < 0) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

int read_on_socket(int sock, void* buffer, size_t length) {
    int read_bytes = 0;
    int to_read = length;
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

int send_on_socket(int sock, void *buffer, size_t size) {
    size_t written_bytes = 0;
    int ret_value;

    while (written_bytes != size) {
        ret_value = write(sock, (char *)buffer + written_bytes, size - written_bytes);

        if (ret_value == 0) {
            printf("Disconnected\n");
            exit(EXIT_FAILURE);
        }

        if (ret_value < 0) {
            perror("writing");
            return -1;
        }
        written_bytes += ret_value;
    }

    return ret_value;
}

void handle_switch_msg(struct message msg, struct client_info *clients, struct client_info *sender, int fds, char* buff) {
    switch (msg.type) {
        case NICKNAME_NEW: {
            size_t len = strlen(msg.infos);
            int valid = (len > 0 && len < NICK_LEN);
            for (size_t k = 0; valid && k < len; k++) {
                if (!isalnum((unsigned char)msg.infos[k])) {
                    valid = 0;
                }
            }

            struct message reply;
            memset(&reply, 0, sizeof(reply));
            reply.type = NICKNAME_NEW;
            char reply_text[MSG_LEN];

            if (!valid) {
                snprintf(reply_text, sizeof(reply_text), "Pseudo invalide\n");
            } else if (client_list_find_by_nickname(clients, msg.infos) != NULL) {
                snprintf(reply_text, sizeof(reply_text), "Pseudo deja utilise\n");
            } else {
                strncpy(sender->nickname, msg.infos, NICK_LEN - 1);
                sender->nickname[NICK_LEN - 1] = '\0';
                snprintf(reply_text, sizeof(reply_text), "Welcome on the chat %s\n", sender->nickname);
            }

            reply.pld_len = strlen(reply_text);
            send_on_socket(fds, &reply, sizeof(reply));
            send_on_socket(fds, reply_text, reply.pld_len);
            break;
        }
        case NICKNAME_LIST: {
            char list_text[MSG_LEN];
            int offset = snprintf(list_text, sizeof(list_text), "Online users are\n");
            for (struct client_info *c = clients; c != NULL; c = c->next) {
                if (c->nickname[0] != '\0' && offset < (int)sizeof(list_text)) {
                    offset += snprintf(list_text + offset, sizeof(list_text) - offset, "  - %s\n", c->nickname);
                }
            }

            struct message reply;
            memset(&reply, 0, sizeof(reply));
            reply.type = NICKNAME_LIST;
            reply.pld_len = strlen(list_text);
            send_on_socket(fds, &reply, sizeof(reply));
            send_on_socket(fds, list_text, reply.pld_len);
            break;
        }
        case NICKNAME_INFOS: {
            struct client_info *target = client_list_find_by_nickname(clients, msg.infos);
            struct message reply;
            memset(&reply, 0, sizeof(reply));
            reply.type = NICKNAME_INFOS;
            char reply_text[NI_MAXHOST + NI_MAXSERV + NICK_LEN + 150];

            if (target == NULL) {
                snprintf(reply_text, sizeof(reply_text), "Utilisateur %s introuvable\n", msg.infos);
            } else {
                char host[NI_MAXHOST], port[NI_MAXSERV];
                getnameinfo((struct sockaddr*)&target->addr, target->addrlen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
                char date_str[64];
                struct tm tm_info;
                localtime_r(&target->connected_at, &tm_info);
                strftime(date_str, sizeof(date_str), "%Y/%m/%d@%H:%M", &tm_info);
                snprintf(reply_text, sizeof(reply_text), "%s connected since %s with IP address %s and port number %s\n", target->nickname, date_str, host, port);
            }
            size_t text_len = strlen(reply_text);
            if (text_len >= MSG_LEN) {
                text_len = MSG_LEN - 1;
                reply_text[text_len] = '\0';
            }
            reply.pld_len = text_len;
            send_on_socket(fds, &reply, sizeof(reply));
            send_on_socket(fds, reply_text, reply.pld_len);
            break;
        }
        case ECHO_SEND: {
            struct message reply;
            memset(&reply, 0, sizeof(reply));
            reply.type = ECHO_SEND;
            reply.pld_len = msg.pld_len;
            send_on_socket(fds, &reply, sizeof(reply));

            if (reply.pld_len > 0) {
                send_on_socket(fds, buff, reply.pld_len);
            }
            break;
        }
        case BROADCAST_SEND: {
            struct message reply;
            memset(&reply, 0, sizeof(reply));
            reply.type = BROADCAST_SEND;
            reply.pld_len = msg.pld_len;
            strncpy(reply.nick_sender, sender->nickname, NICK_LEN - 1);

            for (struct client_info *c = clients; c != NULL; c = c->next) {
                if (c->fd != fds) {
                    send_on_socket(c->fd, &reply, sizeof(reply));
                    if (reply.pld_len > 0) {
                        send_on_socket(c->fd, buff, reply.pld_len);
                    }
                }
            }
            break;
        }
        case UNICAST_SEND: {
            struct client_info *target = client_list_find_by_nickname(clients, msg.infos);
            struct message reply;
            memset(&reply, 0, sizeof(reply));

            if (target == NULL) {
                char err_text[MSG_LEN];
                snprintf(err_text, sizeof(err_text), "Utilisateur %s introuvable\n", msg.infos);
                reply.type = UNICAST_SEND;
                reply.pld_len = strlen(err_text);
                send_on_socket(fds, &reply, sizeof(reply));
                send_on_socket(fds, err_text, reply.pld_len);
            } else {
                reply.type = UNICAST_SEND;
                reply.pld_len = msg.pld_len;
                strncpy(reply.nick_sender, sender->nickname, NICK_LEN - 1);
                send_on_socket(target->fd, &reply, sizeof(reply));
                if (reply.pld_len > 0) {
                    send_on_socket(target->fd, buff, reply.pld_len);
                }
            }
            break;
        }
        default: {
            char* resp = "message received";
            struct message resp_msg;
            memset(&resp_msg, 0, sizeof(resp_msg));
            resp_msg.pld_len = strlen(resp);
            resp_msg.type = ECHO_SEND;
            send_on_socket(fds, &resp_msg, sizeof(resp_msg));
            send_on_socket(fds, resp, resp_msg.pld_len);
            break;
        }
    }
}

void handle_switch_file(struct message msg, struct client_info *clients, struct client_info *sender, int fds, char* buff) {
    switch(msg.type) {
        case FILE_REQUEST: {
            // msg.infos contient le pseudo du destinataire (User2)
            // buff contient le nom du fichier
            struct client_info *target = client_list_find_by_nickname(clients, msg.infos);
            if (target == NULL) {
                struct message reply;
                memset(&reply, 0, sizeof(reply));
                reply.type = FILE_REQUEST;
                char err_text[MSG_LEN];
                snprintf(err_text, sizeof(err_text), "Utilisateur %s introuvable\n", msg.infos);
                reply.pld_len = strlen(err_text);
                send_on_socket(fds, &reply, sizeof(reply));
                send_on_socket(fds, err_text, reply.pld_len);
            } else {
                // On transmet la demande au destinataire en précisant qui est l'expéditeur
                struct message req;
                memset(&req, 0, sizeof(req));
                req.type = FILE_REQUEST;
                req.pld_len = msg.pld_len;
                strncpy(req.nick_sender, sender->nickname, NICK_LEN - 1);
                strncpy(req.infos, msg.infos, NICK_LEN - 1);
                send_on_socket(target->fd, &req, sizeof(req));
                if (req.pld_len > 0) {
                    send_on_socket(target->fd, buff, req.pld_len);
                }
            }
            break;
        }
        // 2. Acceptation du transfert (User2 -> Serveur -> User1) //
        case FILE_ACCEPT: {
            // msg.infos contient le pseudo de l'émetteur initial (User1)
            // buff contient "IP:Port" d'écoute de User2 (ex: "127.0.0.1:8081")
            struct client_info *target = client_list_find_by_nickname(clients, msg.infos);
            if (target != NULL) {
                struct message reply;
                memset(&reply, 0, sizeof(reply));
                reply.type = FILE_ACCEPT;
                reply.pld_len = msg.pld_len;
                strncpy(reply.nick_sender, sender->nickname, NICK_LEN - 1);
                strncpy(reply.infos, msg.infos, NICK_LEN - 1);
                send_on_socket(target->fd, &reply, sizeof(reply));
                if (reply.pld_len > 0) {
                    send_on_socket(target->fd, buff, reply.pld_len);
                }
            }
            break;
        }
        // 3. Refus du transfert (User2 -> Serveur -> User1) //
        case FILE_REJECT: {
            // msg.infos contient le pseudo de l'émetteur initial (User1)
            struct client_info *target = client_list_find_by_nickname(clients, msg.infos);
            if (target != NULL) {
                struct message reply;
                memset(&reply, 0, sizeof(reply));
                reply.type = FILE_REJECT;
                reply.pld_len = msg.pld_len;
                strncpy(reply.nick_sender, sender->nickname, NICK_LEN - 1);
                strncpy(reply.infos, msg.infos, NICK_LEN - 1);
                send_on_socket(target->fd, &reply, sizeof(reply));
                if (reply.pld_len > 0) {
                    send_on_socket(target->fd, buff, reply.pld_len);
                }
            }
            break;
        }
        default:
            break;
    }
}


void handle_clients(struct pollfd fds[FDS_SIZE], int sfd) {
    struct client_info *clients = NULL;
    //init poll fds
    fds[0].fd = sfd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
        for (int i = 1; i < FDS_SIZE; i++) {
        fds[i].fd = -1;
        fds[i].events = 0;
        fds[i].revents = 0;
    }
	while (1) {

        printf("Waiting for activity\n");
        int nb_active_fd = poll(fds, FDS_SIZE, -1);
        die(nb_active_fd, "Polling"); //écoute si il y a de l'activité -> accepte -> redirige vers un nouveau socket d'écoute pour le pld
        for(int i = 0; i < FDS_SIZE; i++) {
            if ( i == 0 && fds[0].revents & POLLIN) { //si socket serv est en écoute d'activité alors:
                struct sockaddr_storage client_addr;
                socklen_t client_addrlen = sizeof(client_addr);
                int client_fd = accept(fds[i].fd, (struct sockaddr*)&client_addr, &client_addrlen); // accepte
                die(client_fd, "Could not accept client connection");
                char host[NI_MAXHOST], port[NI_MAXSERV];
                getnameinfo((struct sockaddr*)&client_addr,   // récupère les infos client
                            client_addrlen, host, sizeof(host),
                            port,
                            sizeof(port),
                            NI_NUMERICHOST | NI_NUMERICSERV);
                printf("Client connected: %s:%s (fd=%d)\n", host, port, client_fd);
                client_list_add(&clients, client_fd, &client_addr, client_addrlen); //Req1.8
                for(size_t j = 0; j < FDS_SIZE; j++) {  //redirige vers le prochain socket dispo dans le tableau fds[] pour gérer le pld
                    if(fds[j].fd == -1) {
                        fds[j].fd = client_fd;
                        fds[j].events = POLLIN;
                        fds[j].revents = 0;
                        break;
                    }
                }
            }
            else if(fds[i].revents & POLLIN) {
                fds[i].revents = 0;
                //read data
                struct message msg;
                memset(&msg, 0, sizeof(msg));
                int ret = read_on_socket(fds[i].fd, &msg, sizeof(msg));
                if (ret <= 0) {
                    client_list_remove(&clients, fds[i].fd);
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    continue;
                }

                if (msg.pld_len >= MSG_LEN) {
                    fprintf(stderr, "Message trop long\n");
                    client_list_remove(&clients, fds[i].fd);
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    continue;
                }

                char buff[MSG_LEN] = {0};
                if (msg.pld_len > 0) {
                    if (read_on_socket(fds[i].fd, buff, msg.pld_len) <= 0) {
                        client_list_remove(&clients, fds[i].fd);
                        close(fds[i].fd);
                        fds[i].fd = -1;
                        continue;
                    }
                    buff[msg.pld_len] = '\0';
                }
                printf("type = %s, nick_sender = %s, payload = %s, from fd = %d\n", msg_type_str[msg.type], msg.nick_sender, buff, fds[i].fd);

                struct client_info *sender = client_list_find_by_fd(clients, fds[i].fd);
                if (sender != NULL && sender->nickname[0] == '\0' && msg.type != NICKNAME_NEW) {
                    struct message reply;
                    memset(&reply, 0, sizeof(reply));
                    reply.type = msg.type;
                    char reply_text[] = "Veuillez d'abord choisir un pseudo avec /nick <pseudo>\n";
                    reply.pld_len = strlen(reply_text);
                    send_on_socket(fds[i].fd, &reply, sizeof(reply));
                    send_on_socket(fds[i].fd, reply_text, reply.pld_len);
                    continue;
                }
                handle_switch_msg(msg, clients, sender, fds[i].fd, buff);
            }
        }
	}
    client_list_destroy(&clients);
    close(sfd);
}

int handle_bind(char* server_port) {
	struct addrinfo hints, *result, *rp;
	int sfd;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if (getaddrinfo(NULL, server_port, &hints, &result) != 0) {
		perror("getaddrinfo()");
		exit(EXIT_FAILURE);
	}
	for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype,
            rp->ai_protocol);
            if (sfd == -1) {
                continue;
            }
        int optval = 1;
            if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
                perror("setsockopt()");
                close(sfd);
                continue;
            }
		if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0) {
			break;
		}
		close(sfd);
	}
	if (rp == NULL) {
		fprintf(stderr, "Could not bind\n");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(result);
	return sfd;
}

int main(int argc, char* argv[]) {
    
    if(argc != 2){
        printf("veuillez saisir le port du serveur\n");
        exit(EXIT_FAILURE);
    }
    char* server_port = argv[1];
    
    int sfd;
    struct pollfd fds[FDS_SIZE] = {};

    sfd = handle_bind(server_port);
    if ((listen(sfd, SOMAXCONN)) != 0) {
        perror("listen()\n");
        exit(EXIT_FAILURE);
    }

    handle_clients(fds, sfd);

    return EXIT_SUCCESS;
}

