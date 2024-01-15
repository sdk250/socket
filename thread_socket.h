#ifndef __THREAD_SOCKET__
#define __THREAD_SOCKET__
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
#include <linux/netfilter_ipv4.h>
#define SIZE 0x80000
#define U_TIMEOUT 500000
#define TIMEOUT 3
#define SERVER_ADDR "110.242.70.69"

extern pthread_attr_t attr;
extern int LOG;
extern int local_fd;
extern char ip[16];

static socklen_t len = sizeof(struct sockaddr);

struct sock_argu {
    int *source;
    int *dest;
    char *buf;
};

int setNonBlocking(int);
void *handle_connection(void *);
void set_socket_timeout(int, unsigned long int, unsigned int);
void *swap_data(void *);
void main_loop(int);
void usage(const char *, int);
void signal_terminate(int);

#endif
