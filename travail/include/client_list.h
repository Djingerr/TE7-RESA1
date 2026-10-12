#include <stdio.h>
#include <sys/socket.h>

struct client;
struct message;

struct client_info *client_list_add(struct client_info **head, int fd, struct sockaddr_storage *addr, socklen_t addrlen);
void client_list_remove(struct client_info **head, int fd);
void client_list_destroy(struct client_info **head);

struct client_info* client_list_find_by_fd(struct client_info *head, int fd);
struct client_info* client_list_find_by_nickname(struct client_info *head, const char *nickname);