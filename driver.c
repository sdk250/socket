#include "thread_socket.h"

void signal_terminate(int sign) {
    close(local_fd);
    exit(EXIT_SUCCESS);
}

void usage(const char *argv, int ret) {
    printf("Usage of %s:\n\t-p\t<PORT>\n\t\tSet PORT while running\n\t-l\tShow running log\n\t-u\t<UID>\n\t\tSet UID while running\n\t-d\tStart daemon service\n\t-h\tShow this message\n", argv);
    exit(ret);
}

void main_loop(int local_fd) {
    for (socklen_t len = sizeof(struct sockaddr); ;) {
        pthread_t tid = 0;
        int *client_fd = (int *) malloc(sizeof(int));
        *client_fd = accept(local_fd, (struct sockaddr *) NULL, &len);
        pthread_create(&tid, &attr, handle_connection, client_fd);
        pthread_detach(tid);
    }
}

void set_socket_timeout(int fd, unsigned long int usec, unsigned int sec) {
    struct timeval tv = {usec, sec};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (void *) &tv, sizeof(struct timeval));
    tv.tv_usec = usec;
    tv.tv_sec = sec;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (void *) &tv, sizeof(tv));
}

void *handle_connection(void *_fd) {
    int server_fd;
    int client_fd = *(int *)_fd;
    char *buffer = NULL, *_buffer = NULL, *url = NULL;
    char method[10] = {0}, http_version[10] = {0};
    struct sockaddr_in server_addr = {0};
    pthread_t t1 = 0, t2 = 0;
    int http = -1;
    char *p = NULL;

    set_socket_timeout(client_fd, 0, TIMEOUT);
    free((int *) _fd);

    buffer = (char *) malloc(sizeof(char) * SIZE);
    _buffer = (char *) malloc(sizeof(char) * SIZE);
    url = (char *) malloc(sizeof(char) * SIZE);

    if (!buffer || !_buffer || !url) {
        close(client_fd);
        free(buffer);
        free(_buffer);
        free(url);
        pthread_exit(NULL);
        return NULL;
    }

    memset(&server_addr, '\0', sizeof(struct sockaddr));
    memset(buffer, '\0', SIZE);
    memset(_buffer, '\0', SIZE);
    memset(url, '\0', SIZE);

    recv_headers(client_fd, buffer, SIZE);

    if (sscanf(buffer, "%9[^ ] %65534[^ ] %9[^ ]\r\n", method, _buffer, http_version) != 3) {
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

    if (http) {
        p = NULL;
        if (strstr(_buffer, "http://")) {
            if (strchr(_buffer + 7, ':'))
                memcpy(url, _buffer + 7, strlen(_buffer));
            else if ((p = strchr(_buffer + 7, '/'))) {
                *p = '\0';
                snprintf(url, SIZE, "%s%s%s", _buffer + 7, ":80/", p + 1);
            } else
                snprintf(url, SIZE, "%s%s", _buffer, ":80");
        } else {
            if ((p = strstr(buffer, "Host: "))) {
                p += 6;
                memcpy(url, p, strstr(p, "\r\n") - p);
                if (!strchr(url, ':'))
                    strcat(url, ":80");
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
    } else
        memcpy(url, _buffer, strlen(_buffer));
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

void *client_to_server(void *par) {
    struct arg *arg_ = (struct arg *) par;
    if (strlen(arg_->buf) == 0) {
        fprintf(stderr, "%s\n", "It's \x1b[31mNULL\x1b[0m");
        pthread_exit(NULL);
        return NULL;
    }
    if (arg_->http)
        send(arg_->dest, arg_->buf, strlen(arg_->buf), MSG_NOSIGNAL);
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

void *server_to_client(void *par) {
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

long int recv_headers(int fd, char *buffer, size_t buffer_size) {
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
        else
            *(buffer + total++) = ch;
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
