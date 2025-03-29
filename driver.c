#include "thread_socket.h"

void signal_terminate(const int sign)
{
    atomic_store_explicit(&SHUTDOWN, true, memory_order_relaxed);
    fprintf(stderr, "Receive terminated signal: %d\n", sign);
    close(local_fd);
}

void usage(const char *argv, const int ret)
{
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

void main_loop(const int local_fd)
{
    for (; !atomic_load(&SHUTDOWN);)
    {
        pthread_t tid;
        int *client_fd = (int *) malloc(sizeof(int));
        if (client_fd == NULL) continue;
        *client_fd = accept(local_fd, (struct sockaddr *) NULL, (socklen_t *) NULL);
        pthread_create(&tid, &attr, handle_connection, client_fd);
        pthread_detach(tid);
    }
    pthread_attr_destroy(&attr);
    puts("Main thread terminated.");
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
    int https = 0;
    long int total = 0;
    struct sockaddr_in server_addr = {0}, destination_addr = {0};
    struct sock_argu args, *_client, *_server;
    char *url = NULL;
    pthread_t t1 = 0, t2 = 0;
    socklen_t len = sizeof(struct sockaddr);

    args.src = *(int *) _fd;
    args.dest = 0;
    args.src_buf = (char *) malloc(SIZE);
    args.dest_buf = (char *) malloc(SIZE);
    url = (char *) malloc(LEN_URL);
    set_socket_timeout(args.src, 0, TIMEOUT);

    if (! args.src_buf || ! args.dest_buf || ! url)
        goto exit_label;

    memset(&server_addr, '\0', sizeof(struct sockaddr));
    memset(&destination_addr, '\0', sizeof(struct sockaddr));
    memset(args.src_buf, '\0', SIZE);
    memset(args.dest_buf, '\0', SIZE);
    memset(url, '\0', LEN_URL);

    if (getsockopt(
        args.src,
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
            ! strstr(args.src_buf, "\r\n\r\n") &&
            (ret = recv(args.src, args.src_buf + total, READ_SIZE, 0));
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
        for (char *p = args.src_buf; (uintptr_t) p != 1 && p - args.src_buf < SIZE; p++)
        {
            if (sscanf(p, "Host: %" LEN_URL_STR "[^ \r\n]\r\n", url) == 1 ||
                sscanf(p, "host: %" LEN_URL_STR "[^ \r\n]\r\n", url) == 1
            )
                break;

            p = strchr(p, '\n');
        }
        if (*url == 0)
        {
            if (sscanf(args.src_buf, "CONNECT %" LEN_URL_STR "[^ ] %*[^ ]\r\n", url) != 1) {
                if (sscanf(args.src_buf, "GET %" LEN_URL_STR "[^ ] %*[^ ]\r\n", url) != 1) {
                    if (sscanf(args.src_buf, "POST %" LEN_URL_STR "[^ ] %*[^ ]\r\n", url) != 1) {
                        perror("Unknown connection.");
                        goto exit_label;
                    }
                }
            }
        }
        for (int i = 0; i < 8; i++)
        {
            if (args.src_buf[i] == "CONNECT "[i])
                https = 1;
            else {
                https = 0;
                break;
            }
        }
    } else {
        if (! inet_ntop(AF_INET, &destination_addr.sin_addr, url, INET_ADDRSTRLEN))
        {
            perror("Translation fail");
            goto exit_label;
        }
        size_t len = strlen(url);
        if (len + 7 > LEN_URL)
        {
            fprintf(stderr, "URI is too long.");
            goto exit_label;
        }
        snprintf(
            url + len,
            LEN_URL,
            ":%u",
            ntohs(destination_addr.sin_port)
        );
    }

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

    if ((args.dest = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        perror("Create socket for zl");
        goto exit_label;
    }
    set_socket_timeout(args.dest, 0, TIMEOUT);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(443);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) != 1)
    {
        perror("Translation address failed");
        goto exit_label;
    }

    if (connect(args.dest,
        (struct sockaddr *) &server_addr,
        sizeof(struct sockaddr)) != 0
    ) {
        perror("Connect to server fail");
        close(args.dest);
        goto exit_label;
    }
    snprintf(
        args.dest_buf,
        SIZE,
        "CONNECT %s HTTP/1.1\r\n\r\n",
        url
    );
    send(args.dest,
        args.dest_buf,
        strlen(args.dest_buf),
        MSG_NOSIGNAL
    );
    memset(args.dest_buf, '\0', strlen(args.dest_buf));
    for (
        short int i = 0, end_i = 39;
        (i = recv(args.dest, args.dest_buf, end_i, 0)) != end_i && i > 0;
        end_i -= i
    ) continue;
    if (! strstr(args.dest_buf, "\r\n\r\n"))
    {
        fprintf(stderr, "Connection is not established: %s\n", url);
        shutdown(args.dest, SHUT_RDWR);
        close(args.dest);
        goto exit_label;
    }
    if (https)
        send(
            args.src,
            args.dest_buf,
            strlen(args.dest_buf),
            MSG_NOSIGNAL
        );
    else
        send(
            args.dest,
            args.src_buf,
            total,
            MSG_NOSIGNAL
        );

    _client = (struct sock_argu *) malloc(sizeof(struct sock_argu));
    _server = (struct sock_argu *) malloc(sizeof(struct sock_argu));

    if (! _client || ! _server)
    {
        free(_client);
        free(_server);
        close(args.dest);
        goto exit_label;
    }

    memset(_client, '\0', sizeof(struct sock_argu));
    memset(_server, '\0', sizeof(struct sock_argu));

    _client->src = args.src;
    _client->src_buf = args.src_buf;
    _client->dest = args.dest;
    _client->dest_buf = args.dest_buf;
    _server->src = _client->dest;
    _server->src_buf = _client->dest_buf;
    _server->dest = _client->src;
    _server->dest_buf = _client->src_buf;

    pthread_create(&t1, NULL, swap_data, _client);
    pthread_create(&t2, NULL, swap_data, _server);

    pthread_join(t2, NULL);
    pthread_join(t1, NULL);

    shutdown(args.dest, SHUT_RDWR);
    close(args.dest);

exit_label:
    close(args.src);
    free(_fd);
    free(args.src_buf);
    free(args.dest_buf);
    free(url);
    args.src_buf = args.dest_buf = url = NULL;
    pthread_exit(NULL);
    return NULL;
}

void *swap_data(void *par)
{
    struct sock_argu *arg_ = (struct sock_argu *) par;
    memset(arg_->src_buf, '\0', SIZE);

    for (long int n = 0; (n = recv(arg_->src, arg_->src_buf, SIZE, 0)); )
    {
        if (n < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            else
                break;
        }
        send(arg_->dest, arg_->src_buf, n, MSG_NOSIGNAL);

        memset(arg_->src_buf, '\0', n);
    }

    shutdown(arg_->src, SHUT_RDWR);
    shutdown(arg_->dest, SHUT_RDWR);
    free(par);
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
