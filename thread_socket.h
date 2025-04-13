#ifndef __THREAD_SOCKET__
#define __THREAD_SOCKET__
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <linux/netfilter_ipv4.h>
#define SIZE 0x80000
#define LEN_URL (SIZE / 2)
#define LEN_URL_STR "262143" // LEN_URL - 1
#define U_TIMEOUT 500000
#define TIMEOUT 3
#define SERVER_ADDR "110.242.70.68"
#define READ_SIZE 0xFF
#define MAX_EVENT (64)

extern pthread_attr_t attr;
extern int LOG;
extern int local_fd;
extern struct sockaddr_in server_addr;
extern atomic_bool SHUTDOWN;

struct server_argu
{
    int32_t src;
    int32_t dst;
    char *msg;
    char *http_msg;
    uint32_t http_msg_len;
};

int setNonBlocking(int);
void set_socket_timeout(int, unsigned long int, unsigned int);
void *swap_data(void *);
void main_loop(int);
void usage(const char *, int);
void signal_terminate(int);
void *handle_server(void *);
void *handle_swap(void *);
extern inline void define_event(
    int32_t epoll_fd,
    struct epoll_event *event,
    int32_t src,
    int32_t dst
);

#endif
