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
    struct timeval tv = {
        .tv_usec = usec,
        .tv_sec = sec
    };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (void *) &tv, sizeof(struct timeval));
    tv.tv_usec = usec;
    tv.tv_sec = sec;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (void *) &tv, sizeof(tv));
}

void *handle_connection(void *_fd) {
    int server_fd = 0;
    int http = -1;
    long int ret = 0, total = 0;
    char *url = NULL;
    char method[10] = {0}, http_version[10] = {0};
    struct sockaddr_in server_addr = {0};
    struct http_header *root = NULL, *cur = NULL;
    struct sock_argu client_to_server = {0}, server_to_client = {0};
    pthread_t t1 = 0, t2 = 0;

    client_to_server.http = server_to_client.http = &http;
    client_to_server.source = server_to_client.dest = (int *) _fd;
    client_to_server.dest = server_to_client.source = &server_fd;
    client_to_server.buf = (char *) malloc(sizeof(char) * SIZE);
    server_to_client.buf = (char *) malloc(sizeof(char) * SIZE);
    url = (char *) malloc(sizeof(char) * 0x100);
    root = (struct http_header *) malloc(sizeof(struct http_header));
    set_socket_timeout(*(client_to_server.source), 0, TIMEOUT);

    if (!client_to_server.buf || !server_to_client.buf || !url || !root)
        goto exit_label;

    memset(&server_addr, '\0', sizeof(struct sockaddr));
    memset(client_to_server.buf, '\0', SIZE);
    memset(server_to_client.buf, '\0', SIZE);
    memset(url, '\0', 0x100);
    root->data = root->prev = root->next = NULL;
    cur = root;

    for (char ch = 0; total <= SIZE; ch = 0) {
        ret = recv(*(client_to_server.source), &ch, 1, 0);
        if (ret == -1) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            else
                break;
        } else if (ret == 0)
            break;
        else
            *(client_to_server.buf + total++) = ch;
        if (strstr(client_to_server.buf, "\r\n")) {
            if (cur->prev == NULL && cur->data == NULL) {
                cur->data = (char *) malloc(sizeof(char) * total + 1);
                if (cur->data == NULL)
                    break;
                memset(cur->data, '\0', total + 1);
                memcpy(cur->data, client_to_server.buf, total);
            } else {
                cur->next = (struct http_header *) malloc(sizeof(struct http_header));
                if (cur->next == NULL)
                    break;
                cur->next->data = (char *) malloc(sizeof(char) * total + 1);
                if (cur->next->data == NULL)
                    break;
                memset(cur->next->data, '\0', total + 1);
                memcpy(cur->next->data, client_to_server.buf, total);
                cur->next->prev = cur;
                cur->next->next = NULL;
                cur = cur->next;
            }
            if (strcmp(client_to_server.buf, "\r\n") == 0)
                break;
            memset(client_to_server.buf, '\0', SIZE);
            total = 0;
        }
    }

    if (root->data && sscanf(root->data, "%9[^ ] %*[^ ] %9[^ ]\r\n", method, http_version) != 2)
        goto exit_label;
    if (strcmp(method, "CONNECT") == 0)
        http = 0;
    else {
        if (strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0)
            http = 1;
        else
            goto exit_label;
    }

    if (http) {
        for (struct http_header *temp = root; temp; temp = temp->next) {
            if (strstr(temp->data, "Host: ")) {
                memcpy(url, temp->data + 6, strchr(temp->data, '\r') - (temp->data + 6));
                if (!strchr(url, ':'))
                    strcat(url, ":80");
                break;
            } else if (temp->next == NULL)
                goto exit_label;
        }
    } else {
        for (struct http_header *temp = root; temp; temp = temp->next) {
            if (strstr(temp->data, "Host: ")) {
                memcpy(url, temp->data + 6, strchr(temp->data, '\r') - (temp->data + 6));
                if (!strchr(url, ':'))
                    strcat(url, ":443");
                break;
            } else if (temp->next == NULL)
                goto exit_label;
        }
    }

    if (LOG)
        printf("URL: %s\n", url);

    if (strstr(url, SERVER_ADDR))
        fprintf(stderr, "%s\n", "\x1b[31mURL ERROR\x1b[0m");
    if ((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("Create socket for zl");
        close(server_fd);
        goto exit_label;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(443);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);
    if ((connect(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr))) < 0) {
        perror("Connect zl");
        close(server_fd);
        goto exit_label;
    }
    sprintf(server_to_client.buf, "CONNECT %s %s\r\n\r\n", url, http_version);
    send(server_fd, server_to_client.buf, strlen(server_to_client.buf), MSG_NOSIGNAL);
    memset(server_to_client.buf, '\0', SIZE);
    recv(server_fd, server_to_client.buf, 39, 0);
    if (http)
        for (struct http_header* s = root; s; s = s->next)
            send(server_fd, s->data, strlen(s->data), MSG_NOSIGNAL);

    pthread_create(&t1, &attr, client_to_server_fn, &client_to_server);
    pthread_create(&t2, &attr, server_to_client_fn, &server_to_client);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    close(server_fd);

exit_label:
    for (struct http_header *s = root; s;) {
        struct http_header *temp = s;
        if (s->data)
            free(s->data);
        s = s->next;
        free(temp);
    }
    close(*(client_to_server.source));
    free((int *) _fd);
    free(client_to_server.buf);
    free(server_to_client.buf);
    free(url);
    pthread_exit(NULL);
    return NULL;
}

void *client_to_server_fn(void *par) {
    struct sock_argu *arg_ = (struct sock_argu *) par;
    memset(arg_->buf, '\0', SIZE);

    for (long int n = 0; (n = recv(*(arg_->source), arg_->buf, SIZE, 0));) {
        if (n == -1) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            else
                break;
        }
        send(*(arg_->dest), arg_->buf, n, MSG_NOSIGNAL);
    }
    shutdown(*(arg_->dest), SHUT_RDWR);
    shutdown(*(arg_->source), SHUT_RDWR);
    pthread_exit(NULL);
    return NULL;
}

void *server_to_client_fn(void *par) {
    struct sock_argu* arg_ = (struct sock_argu *) par;
    if (!*(arg_->http)) {
        if (!strcmp(arg_->buf, "HTTP/1.1 200 Connection established\r\n\r\n"))
            send(*(arg_->dest), arg_->buf, strlen(arg_->buf), MSG_NOSIGNAL);
    }
    memset(arg_->buf, '\0', SIZE);

    for (long int n = 0; (n = recv(*(arg_->source), arg_->buf, SIZE, 0)) > 0;) {
        if (n == -1) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            else
                break;
        }
        send(*(arg_->dest), arg_->buf, n, MSG_NOSIGNAL);
    }
    shutdown(*(arg_->dest), SHUT_RDWR);
    shutdown(*(arg_->source), SHUT_RDWR);
    pthread_exit(NULL);
    return NULL;
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
