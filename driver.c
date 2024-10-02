#include "thread_socket.h"

void signal_terminate(int sign)
{
    pthread_attr_destroy(&attr);
    close(local_fd);
    SHUTDOWN = 1;
    fprintf(stderr, "Receive terminated signal: %d\n", sign);
    exit(EXIT_SUCCESS);
}

void usage(const char *argv, int ret)
{
    printf("Usage of %s:\n"
        "\t-p\t<PORT>\n"
            "\t\tSet PORT while running\n"
        "\t-u\t<UID>\n"
            "\t\tSet UID while running\n"
        "\t-r\t<SERVER ADDRESS>\n"
            "\t\tSet IP of peer\n"
        "\t-H\t<HOST>\n"
            "\t\tSet host of local connection\n"
        "\t-l\tShow running log\n"
        "\t-d\tStart daemon service\n"
        "\t-h\tShow this message\n",
        argv
    );
    exit(ret);
}

void main_loop(int local_fd)
{
    for (; SHUTDOWN == 0;)
    {
        pthread_t tid;
        int *client_fd = (int *) malloc(sizeof(int));
        *client_fd = accept(local_fd, (struct sockaddr *) NULL, (socklen_t *) NULL);
        pthread_create(&tid, &attr, handle_connection, client_fd);
        pthread_detach(tid);
    }
}

void set_socket_timeout(int fd, unsigned long int usec, unsigned int sec)
{
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
    setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        (void *) &tv,
        sizeof(struct timeval)
    );
}

void *handle_connection(void *_fd)
{
    int server_fd = 0, https = 0;
    long int total = 0;
    struct sockaddr_in server_addr = {0}, destination_addr = {0};
    struct sock_argu client_to_server = {0}, server_to_client = {0};
    char *url = NULL;
    pthread_t t1 = 0, t2 = 0;
    pthread_mutex_t lock;
    socklen_t len = sizeof(destination_addr);

    client_to_server.source = server_to_client.dest = (int *) _fd;
    client_to_server.dest = server_to_client.source = &server_fd;
    client_to_server.lock = server_to_client.lock = &lock;
    client_to_server.buf = (char *) malloc(sizeof(char) * SIZE);
    server_to_client.buf = (char *) malloc(sizeof(char) * SIZE);
    url = (char *) malloc(sizeof(char) * LEN_URL);
    set_socket_timeout(*client_to_server.source, 0, TIMEOUT);
    pthread_mutex_init(&lock, NULL);

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
    ) != 0 || (
        (ntohl(destination_addr.sin_addr.s_addr) == 0x0) || // 0.0.0.0/0
        (ntohl(destination_addr.sin_addr.s_addr) & 0xff000000) == 0x0a000000 || // 10.0.0.0/8
        (ntohl(destination_addr.sin_addr.s_addr) & 0xfff00000) == 0xac100000 || // 172.16.0.0/12
        (ntohl(destination_addr.sin_addr.s_addr) & 0xffff0000) == 0xc0a80000 || // 192.168.0.0/16
        (ntohl(destination_addr.sin_addr.s_addr) & 0xff000000) == 0x7f000000 || // 127.0.0.0/8
        (ntohl(destination_addr.sin_addr.s_addr) & 0xffff0000) == 0xa9fe0000 || // 169.254.0.0/16
        (ntohl(destination_addr.sin_addr.s_addr) & 0xf0000000) == 0xe0000000 // 224.0.0.0/4
    )) {
        for (short int ret = 0;
            total + READ_SIZE < SIZE &&
            ! strstr(client_to_server.buf, "\r\n\r\n") &&
            (ret = recv(*client_to_server.source, client_to_server.buf + total, READ_SIZE, 0));
        )
        {
            if (ret == -1)
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                else
                    break;
            } else
                total += ret;
        }
        for (char *p = client_to_server.buf; p && p - client_to_server.buf < SIZE; p += 1)
        {
            if (sscanf(p, "Host: %" LEN_URL_STR "[^ \r\n]\r\n", url) == 1 ||
                sscanf(p, "host: %" LEN_URL_STR "[^ \r\n]\r\n", url) == 1
            )
                break;

            p = strchr(p, '\n');
        }
        for (int i = 0; i < 8; i++)
        {
            if (client_to_server.buf[i] == "CONNECT "[i])
                https = 1;
            else {
                https = 0;
                break;
            }
        }
    } else
        snprintf(
            url,
            LEN_URL,
            "%s:%u",
            inet_ntoa(destination_addr.sin_addr),
            ntohs(destination_addr.sin_port)
        );

    if (*url == 0) goto exit_label;

    if (!strchr(url, ':'))
    {
        if (https)
            strcat(url, ":443");
        else
            strcat(url, ":80");
    }

    if (LOG) printf("URL: %s\n", url);

    if (strstr(url, ip))
    {
        fprintf(stderr, "%s\n", "\x1b[31mURL ERROR\x1b[0m");
        goto exit_label;
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        perror("Create socket for zl");
        goto exit_label;
    }
    set_socket_timeout(server_fd, 0, TIMEOUT);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(443);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(server_fd,
        (struct sockaddr *) &server_addr,
        sizeof(server_addr)) != 0
    ) {
        perror("Connect zl");
        close(server_fd);
        goto exit_label;
    }
    snprintf(
        server_to_client.buf,
        SIZE,
        "CONNECT %s@%s HTTP/1.1\r\n\r\n",
        url,
        *HOST == 0 ? "cloudnproxy.baidu.com" : HOST
    );
    send(server_fd,
        server_to_client.buf,
        strlen(server_to_client.buf),
        MSG_NOSIGNAL
    );
    memset(server_to_client.buf, '\0', strlen(server_to_client.buf));
    for (
        short int i = 0, end_i = 39;
        (i = recv(server_fd, server_to_client.buf, end_i, 0)) != end_i && i > 0;
        end_i -= i
    ) continue;
    if (! strstr(server_to_client.buf, "\r\n\r\n"))
    {
        fprintf(stderr, "Connection is not established.\n");
        shutdown(server_fd, SHUT_RDWR);
        close(server_fd);
        goto exit_label;
    }
    if (https)
        send(
            *client_to_server.source,
            server_to_client.buf,
            strlen(server_to_client.buf),
            MSG_NOSIGNAL
        );
    else
        send(
            *server_to_client.source,
            client_to_server.buf,
            total,
            MSG_NOSIGNAL
        );

    pthread_create(&t1, NULL, swap_data, &client_to_server);
    pthread_create(&t2, NULL, swap_data, &server_to_client);

    pthread_join(t2, NULL);
    pthread_join(t1, NULL);

    close(server_fd);

exit_label:
    pthread_mutex_destroy(&lock);
    close(*client_to_server.source);
    free(client_to_server.source);
    free(client_to_server.buf);
    free(server_to_client.buf);
    free(url);
    client_to_server.buf = server_to_client.buf = url = NULL;
    pthread_exit(NULL);
    return NULL;
}

void *swap_data(void *par)
{
    struct sock_argu *arg_ = (struct sock_argu *) par;
    memset(arg_->buf, '\0', SIZE);

    for (long int n = 0; (n = recv(*arg_->source, arg_->buf, SIZE, 0)); )
    {
        if (n < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            else
                break;
        }
        pthread_mutex_lock(arg_->lock);
        send(*arg_->dest, arg_->buf, n, MSG_NOSIGNAL);
        pthread_mutex_unlock(arg_->lock);

        memset(arg_->buf, '\0', n);
    }

    shutdown(*arg_->dest, SHUT_RDWR);
    shutdown(*arg_->source, SHUT_RDWR);
    pthread_exit(NULL);
    return NULL;
}

int setNonBlocking(int sockfd)
{
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
