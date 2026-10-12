#define MSG_LEN 1024
#define SERV_PORT "8080"
#define SERV_ADDR "127.0.0.1"
#include <stddef.h>


struct info{
    short s;
    long l;
};

int read_on_socket(int sock, void* buffer, size_t length);
int send_on_socket(int sock, void *buffer, size_t size);