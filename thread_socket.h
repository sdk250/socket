#ifndef __THREAD_SOCKET_H__
#define __THREAD_SOCKET_H__

#include "sniff_host.h"
#include "ring_buffer.h"

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
#include <signal.h>
#include <linux/netfilter_ipv4.h>
#define BUFSIZE (0x4000)
#define U_TIMEOUT (500000)
#define TIMEOUT 3
#define SERVER_ADDR "14.215.182.75"
#define MAX_EVENT (128)

extern int LOG;
extern int LOCAL_FD;
extern struct sockaddr_in server_addr;
extern atomic_bool SHUTDOWN;

enum STATE
{
    UNKNOWN = 0,
    WAIT_ESTABLISH,
    ESTABLISHING,
    FORWARDING
};
struct event_t
{
    enum STATE state;
    int32_t src;
    int32_t dst;
    char *msg;
    char *http_msg;
    uint32_t length;
};
struct server_argu
{
    int32_t src;
    int32_t dst;
    char *msg;
    char *http_msg;
    uint32_t http_msg_len;
};

struct Context
{
    int32_t src;
    int32_t dst;
    uint8_t *buf;
    struct ring_buffer rb;
    uint32_t data_len;
    struct Context *peer;
    struct Session *session;
};
struct Session
{
    enum STATE state;
    sniff_result_t host;
    struct Context objs[2];
};

int32_t setNonBlocking(int32_t);
void set_socket_timeout(int, unsigned long int, unsigned int);
void main_loop(const int32_t);
void usage(const char *, const int32_t);
void signal_terminate(const int32_t);

#endif // __THREAD_SOCKET_H__
