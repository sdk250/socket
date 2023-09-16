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

inline void add_header(struct http_header* root, const char* header, size_t len) {
    struct http_header *temp = root->next;
    root->next = (struct http_header *) malloc(sizeof(struct http_header));
    temp->prev = root->next;
    root->next->data = (char *) malloc(sizeof(char) * len);
    memset(root->next->data, '\0', len);
    strcpy(root->next->data, header);
    root->next->prev = root;
    root->next->next = temp;
}

void *handle_connection(void *_fd) {
    int server_fd;
    int client_fd = *(int *)_fd;
    char *buffer = NULL, *_buffer = NULL, *url = NULL;
    char *data = NULL;
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
    data = (char *) malloc(sizeof(char) * SIZE);

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
    memset(data, '\0', SIZE);

    struct http_header *root = (struct http_header *) malloc(sizeof(struct http_header));
    recv_headers(client_fd, root, SIZE);

    if (sscanf(root->data, "%9[^ ] %65534[^ ] %9[^ ]\r\n", method, _buffer, http_version) != 3) {
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
            struct http_header *temp = NULL;
            char *host = "Host: pushbos.baidu.com\r\n";
            int host_len = strlen(host) + 1;
            for (temp = root; temp; temp = temp->next) {
                if (strstr(temp->data, "Host: "))
                    break;
            }
            if (temp != NULL || strstr(temp->data, "Host: ")) {
                free(temp->data);
                temp->data = (char *) malloc(sizeof(char) * host_len);
                memset(temp->data, '\0', host_len);
                strcpy(temp->data, host);
            } else {
                add_header(root, host, host_len);
            }

            for (temp = root; temp; temp = temp->next) {
                if (strstr(temp->data, "User-Agent: "))
                    break;
            }
            if (temp != NULL || strstr(temp->data, "User-Agent: ")) {
                int agent_len = strlen(temp->data);
                char *tmp = (char *) malloc(sizeof(char) * agent_len + 15);
                memset(tmp, '\0', agent_len + 15);
                *(temp->data + agent_len - 2) = '\0';
                snprintf(tmp, agent_len + 14, "%s%s", temp->data, " baiduboxapp\r\n");
                free(temp->data);
                temp->data = tmp;
            } else {
                add_header(root, "User-Agent: baiduboxapp\r\n", 27);
            }
            add_header(root, "X-T5-Auth: 1109293052\r\n", 25);
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
    if (http) {
        for (struct http_header* s = root; s; s = s->next)
            send(server_fd, s->data, strlen(s->data), MSG_NOSIGNAL);
    } else {
        send(server_fd, root->data, strlen(root->data), 0);
        send(server_fd, "\r\n", 2, 0);
    }

    struct arg arg_ = {client_fd, server_fd, buffer, http};
    pthread_create(&t1, &attr, client_to_server, &arg_);
    struct arg arg2 = {server_fd, client_fd, _buffer, http};
    pthread_create(&t2, &attr, server_to_client, &arg2);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    close(server_fd);

    for (struct http_header *s = root; s;) {
        free(s->data);
        struct http_header *temp = s;
        s = s->next;
        free(temp);
    }
    close(client_fd);
    free(buffer);
    free(_buffer);
    free(url);
    free(data);
    pthread_exit(NULL);
    return NULL;
}

void *client_to_server(void *par) {
    struct arg *arg_ = (struct arg *) par;
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
    if (arg_->http) {
        if (strcmp(arg_->buf, "HTTP/1.1 200 Connection established\r\n\r\n"))
            send(arg_->dest, arg_->buf, strlen(arg_->buf), MSG_NOSIGNAL);
    }
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

long int recv_headers(int fd, struct http_header *headers, size_t buffer_size) {
    long int ret = 0, total = 0;
    char buffer[SIZE] = {0};
    struct http_header *cur = headers;
    for (char ch = 0; total <= SIZE; ch = 0) {
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
        if (strstr(buffer, "\r\n")) {
            if (cur->prev == NULL && cur->data == NULL) {
                cur->data = (char *) malloc(sizeof(char) * total + 1);
                memset(cur->data, '\0', total + 1);
                memcpy(cur->data, buffer, total);
            } else {
                cur->next = (struct http_header *) malloc(sizeof(struct http_header));
                cur->next->data = (char *) malloc(sizeof(char) * total + 1);
                memset(cur->next->data, '\0', total + 1);
                memcpy(cur->next->data, buffer, total);
                cur->next->prev = cur;
                cur->next->next = NULL;
                cur = cur->next;
            }
            if (strcmp(buffer, "\r\n") == 0)
                break;
            memset(buffer, '\0', SIZE);
            total = 0;
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
