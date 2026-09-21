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

#define FDS_SIZE 128

#include "common.h"

void die(int ret_value, const char *msg)
{
    if (ret_value < 0) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

int read_on_socket(int sock, void* buffer, size_t length)
{
    int written_bytes = 0;
    int to_write = length;
    int ret_val;
    while(written_bytes != to_write)
    {
        ret_val = read(sock, (char*)(buffer) + written_bytes, to_write - written_bytes);
        if(ret_val == 0)
        {
            printf("disconnected\n");
            printf("client disconnected");
            return ret_val;
        }
        if (ret_val < 0) {
            perror("reading msg header");
            return -1;
        }
        written_bytes += ret_val;
    }
    return ret_val;
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

        if (ret_value < 0) {
            perror("writing");
            return -1;
        }
        written_bytes += ret_value;
    }

    return ret_value;
}

void handle_clients(struct pollfd fds[FDS_SIZE], int sfd) {
    fds[0].fd = sfd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
        for (int i = 1; i < FDS_SIZE; i++)
    {
        fds[i].fd = -1;
        fds[i].events = 0;
        fds[i].revents = 0;
    }
	while (1) {

        printf("Waiting for activity\n");
        int nb_active_fd = poll(fds, FDS_SIZE, -1);
        die(nb_active_fd, "Polling");
        for(int i = 0; i < FDS_SIZE; i++)
        {
            if ( i == 0 && fds[0].revents & POLLIN)
            {
                //listen activity -> should accept -> redirect listen to new fd
                
                //Client accept
                int client_fd = accept(fds[i].fd, NULL, NULL);
                die(client_fd, "Could not accept client connection");
                printf("Client connected\n");
                
                for(size_t j = 0; j < FDS_SIZE; j++)
                {
                    if(fds[j].fd == -1)
                    {
                        fds[j].fd = client_fd;
                        fds[j].events = POLLIN;
                        fds[j].revents = 0;
                        break;
                    }
                }
            }
            else if(fds[i].revents & POLLIN)
            {
                fds[i].revents = 0;
                //read data
                int size_msg;
                int ret = read_on_socket(fds[i].fd, &size_msg, sizeof(size_msg));
                if (ret <= 0) {
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    continue;
                }
        
                if (size_msg >= MSG_LEN) {
                    fprintf(stderr, "Message trop long\n");
                    close(fds[i].fd); 
                    fds[i].fd = -1;
                    continue;
                }
        
                char buff[MSG_LEN] = {0};
                if (read_on_socket(fds[i].fd, buff, size_msg) <= 0) {
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    continue;
                }
                buff[size_msg] = '\0';
                printf("message received : %s\n", buff);
        
                send_all(fds[i].fd, &size_msg, sizeof(int));
                send_all(fds[i].fd, buff, size_msg);
                printf("New client msg: %s\n", buff);
            }
        }
	}
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

