#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#define SIZE 0xFFFF
#define U_TIMEOUT 500000
#define TIMEOUT 3
#define SERVER_ADDR "153.3.236.22"

extern pthread_attr_t attr;
extern int LOG;
extern int local_fd;

struct arg {
    int source;
    int dest;
    char *buf;
    int http;
};

struct http_header {
	char *data;
	struct http_header *prev;
	struct http_header *next;
};

long int recv_headers(int, struct http_header *, size_t);
int setNonBlocking(int);
void *handle_connection(void *);
void set_socket_timeout(int, unsigned long int, unsigned int);
void *client_to_server(void *);
void *server_to_client(void *);
void main_loop(int);
void usage(const char *, int);
void signal_terminate(int);

