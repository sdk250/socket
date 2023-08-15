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
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#define SIZE 0xFFF
#define U_TIMEOUT 500000
#define TIMEOUT 3
#define SERVER_ADDR "153.3.236.22"

long int recv_headers(int, char*, size_t);
int setNonBlocking(int);
void* handle_connection(void *);
void set_socket_timeout(int, unsigned long int, unsigned int);
void* client_to_server(void*);
void* server_to_client(void*);
void main_loop(int);
void usage(const char *, int);
pthread_attr_t attr = {0};
int LOG = 0;

struct arg {
    int source;
    int dest;
    char* buf;
    int http;
};

int main(int argc, char** argv) {
    int local_fd = 0;
    struct sockaddr_in local_addr = {0};
    int on = 1;
    int opt = 0;
    int daemon = 0;
    unsigned int port = 8080;
    pid_t pid = 0;

    if (argc == 1)
        usage(*argv, EXIT_FAILURE);
    for (;(opt = getopt(argc, argv, ":p:dlh")) != -1;) {
        switch(opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'd':
                daemon = 1;
                break;
            case 'l':
                LOG = 1;
                break;
            case 'h':
                usage(*argv, EXIT_SUCCESS);
                break;
            case ':':
                printf("Missing argument after: -%c\n", optopt);
                usage(*argv, EXIT_FAILURE);
                break;
            case '?':
                printf("Invalid argument: -%c\n", optopt);
                usage(*argv, EXIT_FAILURE);
                // break;
            default:
                usage(*argv, EXIT_FAILURE);
        }
    }

    signal(SIGPIPE, SIG_IGN);
    if ((local_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("Create local server socket file descriptor fail");
        close(local_fd);
        exit(EXIT_FAILURE);
    }
    setsockopt(local_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&local_addr, '\0', sizeof(struct sockaddr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port);
    local_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(local_fd, (const struct sockaddr*)&local_addr, sizeof(struct sockaddr)) < 0) {
        fprintf(stderr, "Bind %s:%u error", inet_ntoa(local_addr.sin_addr), port);
        perror(" ");
        exit(EXIT_FAILURE);
        close(local_fd);
    }
    if (listen(local_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
        close(local_fd);
    }
    printf("Listen on %s:%u.\n", inet_ntoa(local_addr.sin_addr), port);
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256*1024);
    if (daemon) {
        if ((pid = fork()) == 0) {
            pid_t sid = setsid();
            if (sid < 0) {
                perror("setsid error");
                exit(EXIT_FAILURE);
            }
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            main_loop(local_fd);
            close(local_fd);
        } else {
            printf("PID of %s is %d\n", *argv, pid);
            close(local_fd);
        }
    } else {
        main_loop(local_fd);
        close(local_fd);
    }

    puts("Done.");
    return 0;
}

void usage(const char* argv, int ret) {
    printf("Usage of %s:\n\t-p\t<port for running>\n\t-l\tshow running log\n\t-d\tstart daemon service\n\t-h\tshow this message\n", argv);
    exit(ret);
}

void main_loop(int local_fd) {
    socklen_t len = sizeof(struct sockaddr);
    pthread_t tid = 0;

    for (; ;) {
        int *client_fd = (int *) malloc(sizeof(int));
        static struct sockaddr_in client_addr = {0};
        memset(&client_addr, '\0', sizeof(struct sockaddr));
        *client_fd = accept(local_fd, (struct sockaddr*)&client_addr, &len);
        pthread_create(&tid, &attr, handle_connection, client_fd);
    }
}

void set_socket_timeout(int fd, unsigned long int usec, unsigned int sec) {
    struct timeval tv = {usec, sec};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (void*) &tv, sizeof(struct timeval));
    tv.tv_usec = usec;
    tv.tv_sec = sec;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (void*) &tv, sizeof(tv));
}

void* handle_connection(void* _fd) {
    int server_fd;
    int client_fd = *(int*)_fd;
    char* buffer = NULL, *_buffer = NULL, *url = NULL;
    char method[10] = {0}, http_version[10] = {0};
    struct sockaddr_in server_addr = {0};
    pthread_t t1 = 0, t2 = 0;
    int http = -1;
    char *p = NULL;

    set_socket_timeout(client_fd, 0, TIMEOUT);
    free((int *) _fd);

    buffer = (char*) malloc(sizeof(char) * SIZE);
    _buffer = (char*) malloc(sizeof(char) * SIZE);
    url = (char*) malloc(sizeof(char) * SIZE);

    memset(&server_addr, '\0', sizeof(struct sockaddr));
    memset(buffer, '\0', SIZE);
    memset(_buffer, '\0', SIZE);
    memset(url, '\0', SIZE);

    recv_headers(client_fd, buffer, SIZE);
    // printf("DATA: \n\x1b[31m====\t====\t====\t====\x1b[0m\n%s\n\x1b[32m====\t====\t====\t====\x1b[0m\n", buffer);

    if (sscanf(buffer, "%9[^ ] %229376[^ ] %9[^ ]\r\n", method, _buffer, http_version) != 3) {
        close(client_fd);
        free(buffer);
        free(_buffer);
        free(url);
        pthread_exit(NULL);
        return NULL;
    }
    if (strcmp(method, "CONNECT") == 0) {
        http = 0;
    } else {
        if (strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0)
            http = 1;
        else {
            close(client_fd);
            free(buffer);
            free(_buffer);
            free(url);
            pthread_exit(NULL);
            return NULL;
        }
    }

    // char* p = strchr(buffer, ' ') + 1;
    if (http) {
        p = NULL;
        if (strstr(_buffer, "http://")) {
            if (strchr(_buffer + 7, ':')) {
                memcpy(url, _buffer + 7, strlen(_buffer));
            } else if ((p = strchr(_buffer + 7, '/'))) {
                *p = '\0';
                snprintf(url, SIZE, "%s%s%s", _buffer + 7, ":80/", p + 1);
            } else {
                snprintf(url, SIZE, "%s%s", _buffer, ":80");
            }
        } else {
            if ((p = strstr(buffer, "Host: "))) {
                p += 6;
                memcpy(url, p, strstr(p, "\r\n") - p);
            } else {
                puts("\x1b[31mWARNNING\x1b[0m");
                close(client_fd);
                free(buffer);
                free(_buffer);
                free(url);
                pthread_exit(NULL);
                return NULL;
            }
        }
    } else {
        memcpy(url, _buffer, strlen(_buffer));
    }
    if (LOG)
        printf("URL: %s\n", url);

    if (strstr(url, SERVER_ADDR))
        fprintf(stderr, "%s\n", "\x1b[31mURL ERROR\x1b[0m");
    if ((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("Create socket for zl");
        close(client_fd);
        close(server_fd);
        free(buffer);
        free(_buffer);
        free(url);
        pthread_exit(NULL);
        return NULL;
    }
    // set_socket_timeout(server_fd, 0, TIMEOUT);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(443);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);
    if ((connect(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr))) < 0) {
        perror("Connect zl");
        close(client_fd);
        close(server_fd);
        free(buffer);
        free(_buffer);
        free(url);
        pthread_exit(NULL);
        return NULL;
    }
    memset(_buffer, '\0', SIZE);
    snprintf(_buffer, SIZE, "CONNECT %s%s\r\nHost: 153.3.236.22\r\nUser-Agent: baiduboxapp\r\nX-T5-Auth: 683556443\r\n\r\n", url, http_version);
    send(server_fd, _buffer, strlen(_buffer), MSG_NOSIGNAL);
    recv_headers(server_fd, _buffer, SIZE);

    struct arg arg_ = {client_fd, server_fd, buffer, http};
    pthread_create(&t1, &attr, client_to_server, &arg_);
    struct arg arg2 = {server_fd, client_fd, _buffer, http};
    pthread_create(&t2, &attr, server_to_client, &arg2);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    close(client_fd);
    close(server_fd);
    free(buffer);
    free(_buffer);
    free(url);
    pthread_exit(NULL);
    return NULL;
}

void* client_to_server(void* par) {
    struct arg* arg_ = (struct arg*) par;
    if (strlen(arg_->buf) == 0) {
        fprintf(stderr, "%s\n", "It's \x1b[31mNULL\x1b[0m");
        pthread_exit(NULL);
        return NULL;
    }
    if (arg_->http) {
        send(arg_->dest, arg_->buf, strlen(arg_->buf), MSG_NOSIGNAL);
    }
    memset(arg_->buf, '\0', SIZE);
    for (long int n = 0; (n = recv(arg_->source, arg_->buf, SIZE, 0));) {
        if (n == -1) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            else
                break;
        }
        send(arg_->dest, arg_->buf, n, MSG_NOSIGNAL);
    }
    shutdown(arg_->dest, SHUT_RDWR);
    shutdown(arg_->source, SHUT_RDWR);
    pthread_exit(NULL);
    return NULL;
}

void* server_to_client(void* par) {
    struct arg* arg_ = (struct arg*) par;
    if (!arg_->http)
        send(arg_->dest, arg_->buf, strlen(arg_->buf), MSG_NOSIGNAL);
    else
        if (strcmp(arg_->buf, "HTTP/1.1 200 Connection established\r\n\r\n"))
            send(arg_->dest, arg_->buf, strlen(arg_->buf), MSG_NOSIGNAL);
    memset(arg_->buf, '\0', SIZE);

    for (long int n = 0; (n = recv(arg_->source, arg_->buf, SIZE, 0)) > 0;) {
        if (n == -1) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            else
                break;
        }
        send(arg_->dest, arg_->buf, n, MSG_NOSIGNAL);
    }
    shutdown(arg_->dest, SHUT_RDWR);
    shutdown(arg_->source, SHUT_RDWR);
    pthread_exit(NULL);
    return NULL;
}

long int recv_headers(int fd, char* buffer, size_t buffer_size) {
    long int ret = 0, total = 0;
    memset(buffer, '\0', buffer_size);
    for (char ch = 0; total <= buffer_size; ch = 0) {
        ret = recv(fd, &ch, 1, 0);
        if (ret == -1) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            else
                break;
        } else if (ret == 0)
            break;
        else {
            *(buffer + total++) = ch;
        }
        if (strstr(buffer, "\r\n\r\n")) {
            *(buffer + total + 1) = '\0';
            break;
        }
    }
    return total;
}

int setNonBlocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl(sockfd, F_SETFL, flags) == -1) {
        perror("fcntl");
        return -1;
    }

    return 0;
}
