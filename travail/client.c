#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

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

            if (fgets(buff, MSG_LEN, stdin) == NULL) {
                break;
            }

            int size_msg = strlen(buff);

            send_all(sockfd, &size_msg, sizeof(int));
            send_all(sockfd, buff, size_msg);

            /* Req1.7 */
            if (strcmp(buff, "/quit\n") == 0) {
                printf("Déconnexion...\n");
                break;
            }
        }

        if (fds[1].revents & POLLIN) {
            memset(buff, 0, MSG_LEN);

            int size_recv;

            recv_all(sockfd, &size_recv, sizeof(int));

            if (size_recv < 0 || size_recv >= MSG_LEN) {
                fprintf(stderr, "Taille de message invalide\n");
                break;
            }
            recv_all(sockfd, buff, size_recv);
            buff[size_recv] = '\0'; // Sinon pas de fins pour la chaine de caractère. Faire avant d'afficher.
            printf("Received: %s", buff);
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

