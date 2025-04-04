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

void *handle_swap(void *arg)
{
    int32_t epoll_fd = *(int32_t *) arg;
    char *buf = (char *) arg;
    memset(buf, '\0', sizeof(int32_t));
    struct epoll_event events[MAX_EVENT];

    for (int event_count = 0; !atomic_load(&SHUTDOWN);)
    {
        event_count = epoll_wait(epoll_fd, events, MAX_EVENT, -1);
        for (int i = 0; i < event_count; i++)
        {
            if (events[i].events & EPOLLIN)
            {
                int32_t src = *(int32_t *) &events[i].data.u64;
                int32_t dst = *(((int32_t *) &events[i].data.u64) + 1);
                bool _continue = false;

                int recved = 0;
                for (;
                    (recved = recv(src, buf, SIZE, 0));
                )
                {
                    if (recved < 0)
                    {
                        _continue = (errno == EAGAIN || errno == EWOULDBLOCK);
                        break;
                    }
                    send(dst, buf, recved, MSG_NOSIGNAL);
                    memset(buf, '\0', recved);
                }
                if (!_continue || recved == 0)
                {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, src, NULL);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, dst, NULL);
                    close(src);
                    close(dst);
                }
            }
        }
    }
    pthread_exit(NULL);
    return NULL;
}
void *handle_server(void *arg)
{
    int32_t epoll_fd = *(int32_t *) arg;
    int32_t swap_epoll_fd = *(((int32_t *) arg) + 1);
    char *buf = (char *) arg;
    memset(buf, '\0', sizeof(int64_t));
    struct epoll_event event, events[MAX_EVENT];

    for (int event_count = 0; !atomic_load(&SHUTDOWN);)
    {
        event_count = epoll_wait(epoll_fd, events, MAX_EVENT, -1);
        for (int i = 0; i < event_count; i++)
        {
            struct server_argu *server_ptr = (struct server_argu *) events[i].data.ptr;
            int32_t src = server_ptr->src;
            int32_t dst = server_ptr->dst;

            if (events[i].events & EPOLLOUT)
            {
                send(
                    dst,
                    server_ptr->msg,
                    strlen(server_ptr->msg),
                    MSG_NOSIGNAL
                );

                free(server_ptr->msg);
                server_ptr->msg = NULL;

                memset(&event, '\0', sizeof(struct epoll_event));
                event.events = EPOLLIN | EPOLLET;
                event.data.ptr = events[i].data.ptr;
                events[i].data.ptr = NULL;
                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, dst, &event);
            } else if (events[i].events & EPOLLIN) {
                for (
                    short int i = 0, end_i = 39;
                    (i = recv(dst, buf, end_i, 0)) != end_i && i > 0;
                    end_i -= i
                ) continue;
                if (! strstr(buf, "\r\n\r\n"))
                {
                    memset(buf, '\0', strlen(buf));
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, dst, NULL);
                    shutdown(dst, SHUT_RDWR);
                    close(dst);
                    close(src);
                    free(server_ptr);
                    continue;
                }

                if (server_ptr->http_msg == NULL)
                    send(
                        src,
                        buf,
                        strlen(buf),
                        MSG_NOSIGNAL
                    );
                else
                    send(
                        dst,
                        server_ptr->http_msg,
                        strlen(server_ptr->http_msg),
                        MSG_NOSIGNAL
                    );
                memset(buf, '\0', strlen(buf));
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, dst, NULL);
                free(server_ptr);

                memset(&event, '\0', sizeof(struct epoll_event));
                *(int32_t *) &event.data.u64 = src;
                *(((int32_t *) &event.data.u64) + 1) = dst;
                event.events = EPOLLIN | EPOLLET;
                epoll_ctl(swap_epoll_fd, EPOLL_CTL_ADD, src, &event);

                memset(&event, '\0', sizeof(struct epoll_event));
                *(int32_t *) &event.data.u64 = dst;
                *(((int32_t *) &event.data.u64) + 1) = src;
                event.events = EPOLLIN | EPOLLET;
                epoll_ctl(swap_epoll_fd, EPOLL_CTL_ADD, dst, &event);
            }
        }
    }
    pthread_exit(NULL);
    return NULL;
}
void main_loop(const int local_fd)
{
    int epoll_fd, event_count;
    struct epoll_event event, events[MAX_EVENT];
    char *thread_buf_swap = calloc(SIZE, sizeof(char));
    char *thread_buf_server = calloc(SIZE, sizeof(char));
    char *buf = calloc(SIZE, sizeof(char));
    char *_buf = calloc(SIZE, sizeof(char));
    char *url = calloc(LEN_URL, sizeof(char));
    if (! thread_buf_swap || ! buf || ! _buf || ! thread_buf_server || ! url)
    {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    epoll_fd = epoll_create1(0);
    int32_t swap_epoll_fd = epoll_create1(0);
    int32_t server_epoll_fd = epoll_create1(0);
    *(int32_t *) thread_buf_swap = swap_epoll_fd;
    *(int32_t *) thread_buf_server = server_epoll_fd;
    *(((int32_t *) thread_buf_server) + 1) = swap_epoll_fd;
    pthread_t tid_swap, tid_server;
    pthread_create(&tid_swap, &attr, handle_swap, thread_buf_swap);
    pthread_create(&tid_server, &attr, handle_server, thread_buf_server);
    // pthread_detach(tid_swap);

    event.events = EPOLLIN;
    event.data.fd = local_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, local_fd, &event);
    for (; !atomic_load(&SHUTDOWN);)
    {
        event_count = epoll_wait(epoll_fd, events, MAX_EVENT, -1);
        for (int i = 0; i < event_count; i++)
        {
            int fd = events[i].data.fd;
            if (fd == local_fd)
            {
                int _fd = accept(fd, NULL, NULL);
                if (_fd < 0)
                    continue;

                setNonBlocking(_fd);
                event.events = EPOLLIN | EPOLLET;
                event.data.fd = _fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, _fd, &event);
            } else if (events[i].events & EPOLLIN) {
                int total = 0, https = 0, dst = 0;
                struct sockaddr_in server_addr, destination_addr;
                socklen_t len = sizeof(struct sockaddr);

                memset(&destination_addr, '\0', sizeof(struct sockaddr));
                if (getsockopt(
                    fd,
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
                        ! strstr(buf, "\r\n\r\n") &&
                        (ret = recv(fd, buf + total, READ_SIZE, 0));
                    )
                    {
                        if (ret == -1)
                            break;
                        else
                            total += ret;
                    }
    
                    for (char *p = buf; (uintptr_t) p != 1 && p - buf < SIZE; p++)
                    {
                        if (sscanf(p, "Host: %" LEN_URL_STR "[^ \r\n]\r\n", url) == 1 ||
                            sscanf(p, "host: %" LEN_URL_STR "[^ \r\n]\r\n", url) == 1
                        )
                            break;
    
                        p = strchr(p, '\n');
                    }
                    if (*url == 0)
                    {
                        if (sscanf(buf, "CONNECT %" LEN_URL_STR "[^ ] %*[^ ]\r\n", url) != 1) {
                            if (sscanf(buf, "GET %" LEN_URL_STR "[^ ] %*[^ ]\r\n", url) != 1) {
                                if (sscanf(buf, "POST %" LEN_URL_STR "[^ ] %*[^ ]\r\n", url) != 1) {
                                    perror("Unknown connection.");
                                    memset(buf, '\0', strlen(buf));
                                    memset(url, '\0', strlen(url));
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                                    close(fd);
                                    continue;
                                }
                            }
                        }
                    }
                    for (int i = 0; i < 8; i++)
                    {
                        if (buf[i] == "CONNECT "[i])
                            https = 1;
                        else {
                            https = 0;
                            break;
                        }
                    }
                    if (!strchr(url, ':'))
                    {
                        if (https)
                            strncat(url, ":443", LEN_URL);
                        else
                            strncat(url, ":80", LEN_URL);
                    }
                } else {
                    if (! inet_ntop(AF_INET, &destination_addr.sin_addr, url, INET_ADDRSTRLEN))
                    {
                        perror("Translation fail");
                        memset(buf, '\0', strlen(buf));
                        memset(url, '\0', strlen(url));
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);
                        continue;
                    }
                    size_t len = strlen(url);
                    if (len + 7 > LEN_URL)
                    {
                        fprintf(stderr, "URI is too long: %s\n", url);
                        memset(buf, '\0', strlen(buf));
                        memset(url, '\0', strlen(url));
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);
                        continue;
                    }
                    snprintf(
                        url + len,
                        LEN_URL,
                        ":%u",
                        ntohs(destination_addr.sin_port)
                    );
                }

                if (LOG) printf("url: %s\n", url);

                memset(&server_addr, '\0', sizeof(struct sockaddr));
                if ((dst = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
                {
                    perror("Create socket for zl");
                    memset(buf, '\0', strlen(buf));
                    memset(url, '\0', strlen(url));
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    continue;
                }
                setNonBlocking(dst);
                set_socket_timeout(dst, 0, TIMEOUT);

                server_addr.sin_family = AF_INET;
                server_addr.sin_port = htons(443);
                if (inet_pton(AF_INET, ip, &server_addr.sin_addr) != 1)
                {
                    perror("Translation address failed");
                    close(dst);
                    memset(buf, '\0', strlen(buf));
                    memset(url, '\0', strlen(url));
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    continue;
                }

                if (connect(
                    dst,
                    (struct sockaddr *) &server_addr,
                    sizeof(struct sockaddr)) == -1 && errno != EINPROGRESS
                ) {
                    perror("Connect to server fail");
                    close(dst);
                    memset(buf, '\0', strlen(buf));
                    memset(url, '\0', strlen(url));
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    continue;
                }
                snprintf(
                    _buf,
                    SIZE,
                    "CONNECT %s HTTP/1.1\r\n\r\n",
                    url
                );
                memset(&event, '\0', sizeof(struct epoll_event));
                event.events = EPOLLOUT | EPOLLET;
                event.data.ptr = calloc(1, sizeof(struct server_argu));
                if (event.data.ptr == NULL)
                {
                    perror("calloc");
                    close(dst);
                    memset(buf, '\0', strlen(buf));
                    memset(_buf, '\0', strlen(_buf));
                    memset(url, '\0', strlen(url));
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    continue;
                }
                ((struct server_argu *) event.data.ptr)->src = fd;
                ((struct server_argu *) event.data.ptr)->dst = dst;
                ((struct server_argu *) event.data.ptr)->msg = strdup(_buf);
                if (!https)
                    ((struct server_argu *) event.data.ptr)->http_msg = strdup(buf);
                epoll_ctl(server_epoll_fd, EPOLL_CTL_ADD, dst, &event);

                memset(buf, '\0', strlen(buf));
                memset(_buf, '\0', strlen(_buf));
                memset(url, '\0', strlen(url));
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            }
        }
    }

    close(epoll_fd);
    pthread_join(tid_swap, NULL);
    pthread_join(tid_server, NULL);
    close(swap_epoll_fd);
    close(server_epoll_fd);
    free(thread_buf_swap);
    free(thread_buf_server);
    free(buf);
    free(_buf);
    free(url);
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
