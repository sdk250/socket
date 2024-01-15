#include "thread_socket.h"

void signal_terminate(int sign) {
    close(local_fd);
    exit(EXIT_SUCCESS);
}

void usage(const char *argv, int ret) {
    printf("Usage of %s:\n"
        "\t-p\t<PORT>\n"
            "\t\tSet PORT while running\n"
        "\t-u\t<UID>\n"
            "\t\tSet UID while running\n"
        "\t-r\t<SERVER ADDRESS>\n"
            "\t\tSet IP of peer\n"
        "\t-l\tShow running log\n"
        "\t-d\tStart daemon service\n"
        "\t-h\tShow this message\n",
        argv
    );
    exit(ret);
}

void main_loop(int local_fd) {
    for (pthread_t tid = 0; ;) {
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
    setsockopt(
        fd,
        SOL_SOCKET,
        SO_SNDTIMEO,
        (void *) &tv,
        sizeof(struct timeval)
    );
    tv.tv_usec = usec;
    tv.tv_sec = sec;
    setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        (void *) &tv,
        sizeof(tv)
    );
}

void *handle_connection(void *_fd) {
    int server_fd = 0, https = 0;
    long int total = 0;
    struct sockaddr_in server_addr = {0}, destination_addr = {0};
    struct sock_argu client_to_server = {0}, server_to_client = {0};
    char *url = NULL, *ch = NULL;
    pthread_t t1 = 0, t2 = 0;

    client_to_server.source = server_to_client.dest = (int *) _fd;
    client_to_server.dest = server_to_client.source = &server_fd;
    client_to_server.buf = (char *) malloc(sizeof(char) * SIZE);
    server_to_client.buf = (char *) malloc(sizeof(char) * SIZE);
    url = (char *) malloc(sizeof(char) * LEN_URL);
    set_socket_timeout(*(client_to_server.source), 0, TIMEOUT);

    if (! client_to_server.buf || ! server_to_client.buf || ! url)
        goto exit_label;

    memset(&server_addr, '\0', sizeof(struct sockaddr));
    memset(&destination_addr, '\0', sizeof(struct sockaddr));
    memset(client_to_server.buf, '\0', SIZE);
    memset(server_to_client.buf, '\0', SIZE);
    memset(url, '\0', LEN_URL);

    if (getsockopt(
        *client_to_server.source,
        SOL_IP,
        SO_ORIGINAL_DST,
        &destination_addr,
        &len
    ) < 0)
    {
        perror("getsockopt SO_ORIGINAL_DST failed");
        goto exit_label;
    }

    if (strcmp(inet_ntoa(destination_addr.sin_addr), "127.0.0.1") == 0) {
        for (; total <= SIZE; ) {
            char ch = 0;
            short int ret = recv(*client_to_server.source, &ch, 1, 0);
            if (ret == -1) {
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                else
                    break;
            } else if (ret == 0)
                break;
            else
                *(client_to_server.buf + total++) = ch;

            if (strstr(client_to_server.buf, "\r\n\r\n"))
                break;
        }
        if (sscanf(client_to_server.buf, "CONNECT %" LEN_URL_STR "[^ ] %*[^ ]\r\n", url) != 1) {
            if (sscanf(client_to_server.buf, "GET %" LEN_URL_STR "[^ ] %*[^ ]\r\n", url) != 1) {
                if (sscanf(client_to_server.buf, "POST %" LEN_URL_STR "[^ ] %*[^ ]\r\n", url) != 1) {
                    perror("Unknown connection.");
                    goto exit_label;
                }
            }
        } else https = 1;
    } else
        snprintf(
            url,
            LEN_URL,
            "%s:%u",
            inet_ntoa(destination_addr.sin_addr),
            ntohs(destination_addr.sin_port)
        );

    if ((ch = strstr(url, "//"))) {
        ch += 2;
        unsigned int len = ch - url + 1;
        char *p = NULL;
        char *p1 = NULL;
        strncpy(url, ch, len);
        memset(url + len, '\0', LEN_URL - strlen(url) + len);
        if ((p = strchr(ch, '/'))) *p = '\0';
        (p1 = strchr(ch, ':')) ? NULL : strcat(ch, ":80");
    }

    if (LOG)
        printf("URL: %s\n", url);

    if (strstr(url, ip))
    {
        fprintf(stderr, "%s\n", "\x1b[31mURL ERROR\x1b[0m");
        goto exit_label;
    }
    if ((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("Create socket for zl");
        close(server_fd);
        goto exit_label;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(443);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Connect zl");
        close(server_fd);
        goto exit_label;
    }
    sprintf(
        server_to_client.buf,
        "CONNECT %s HTTP/1.1\r\n\r\n",
        url
    );
    send(server_fd, server_to_client.buf, strlen(server_to_client.buf), MSG_NOSIGNAL);
    memset(server_to_client.buf, '\0', SIZE);
    recv(server_fd, server_to_client.buf, 39, 0);
    if (https)
        send(
            *client_to_server.source,
            server_to_client.buf,
            strlen(server_to_client.buf),
            MSG_NOSIGNAL
        );
    else
        send(
            *client_to_server.dest,
            client_to_server.buf,
            total,
            MSG_NOSIGNAL
        );

    pthread_create(&t1, &attr, swap_data, &client_to_server);
    pthread_create(&t2, &attr, swap_data, &server_to_client);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    close(server_fd);

exit_label:
    close(*client_to_server.source);
    free(client_to_server.source);
    free(client_to_server.buf);
    free(server_to_client.buf);
    free(url);
    pthread_exit(NULL);
    return NULL;
}

void *swap_data(void *par) {
    struct sock_argu *arg_ = (struct sock_argu *) par;
    memset(arg_->buf, '\0', SIZE);

    for (long int n = 0; (n = recv(*arg_->source, arg_->buf, SIZE, 0)); ) {
        if (n == -1) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            else
                break;
        }
        send(*arg_->dest, arg_->buf, n, MSG_NOSIGNAL);
    }
    shutdown(*arg_->dest, SHUT_RDWR);
    shutdown(*arg_->source, SHUT_RDWR);
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
