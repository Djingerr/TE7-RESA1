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

void handle_clients(struct pollfd fds[FDS_SIZE], int sfd) {
    struct client_info *clients = NULL;

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
        die(nb_active_fd, "Polling");
        for(int i = 0; i < FDS_SIZE; i++) {
            if ( i == 0 && fds[0].revents & POLLIN) {
                //listen activity -> should accept -> redirect listen to new fd
                
                //Client accept
                struct sockaddr_storage client_addr;
                socklen_t client_addrlen = sizeof(client_addr);
                int client_fd = accept(fds[i].fd, (struct sockaddr*)&client_addr, &client_addrlen);
                die(client_fd, "Could not accept client connection");

                char host[NI_MAXHOST], port[NI_MAXSERV];
                getnameinfo((struct sockaddr*)&client_addr, client_addrlen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
                printf("Client connected: %s:%s (fd=%d)\n", host, port, client_fd);

                client_list_add(&clients, client_fd, &client_addr, client_addrlen); //Req1.8

                for(size_t j = 0; j < FDS_SIZE; j++) {
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
                if(strncmp(buff,"/quit",5) == 0) {
                    client_list_remove(&clients, fds[i].fd);
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    continue;
                }
                char* resp = "message received";
                struct message resp_msg;
                memset(&resp_msg, 0, sizeof(resp_msg));
                resp_msg.pld_len = strlen(resp);
                resp_msg.type = ECHO_SEND;
                send_on_socket(fds[i].fd, &resp_msg, sizeof(resp_msg));
                send_on_socket(fds[i].fd, resp, resp_msg.pld_len);
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

