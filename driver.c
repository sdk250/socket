#include <sys/time.h>

#include "thread_socket.h"

static pthread_t tid_swap = 0, tid_server = 0;
const uint8_t RSP_LEN = 39;

void signal_terminate(const int sign)
{
    atomic_store_explicit(&SHUTDOWN, true, memory_order_release);
    fprintf(stderr, "Receive terminated signal: %d\n", sign);
    pthread_join(tid_swap, NULL);
    pthread_join(tid_server, NULL);
    close(local_fd);
    pthread_attr_destroy(&attr);
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
    int32_t epoll_fd = 0, event_count = 0;
    struct epoll_event event, events[MAX_EVENT];
    char *thread_buf_swap = calloc(SIZE, sizeof(char));
    char *thread_buf_server = calloc(SIZE, sizeof(char));
    char *buf = calloc(SIZE, sizeof(char));
    char *_buf = calloc(LEN_URL, sizeof(char));
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
    // pthread_create(&tid_swap, &attr, handle_swap, thread_buf_swap);
    // pthread_create(&tid_server, &attr, handle_server, thread_buf_server);

    event.events = EPOLLIN;
    event.data.fd = local_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, local_fd, &event);
    for (; ! atomic_load_explicit(&SHUTDOWN, memory_order_acquire); )
    {
        event_count = epoll_wait(epoll_fd, events, MAX_EVENT, -1);
        for (int32_t i = 0; i < event_count; i++)
        {
            int32_t fd = events[i].data.fd;
            if (fd == local_fd)
            {
                int32_t client_fd = accept(fd, NULL, NULL);
                if (client_fd < 0)
                    continue;

                setNonBlocking(client_fd);
                struct event_t *ep = calloc(sizeof(char), sizeof(struct event_t));
                if (ep == NULL)
                {
                    close(client_fd);
                    continue;
                }
                ep->src = client_fd;
                ep->state = WAIT_ESTABLISH;
                memset(&event, '\0', sizeof(event));
                event.events = EPOLLIN | EPOLLET;
                event.data.ptr = ep;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
            }
            else if (events[i].events & EPOLLIN)
            {
                struct event_t *ep = (struct event_t *) events[i].data.ptr;
                switch (ep->state)
                {
                    case WAIT_ESTABLISH:
                    {
                        uint32_t total = 0;
                        bool https = false;
                        struct sockaddr_in destination_addr;
                        socklen_t len = sizeof(struct sockaddr);

                        memset(&destination_addr, '\0', sizeof(struct sockaddr));
                        if (getsockopt(
                            ep->src,
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
                        ))
                        {
                            for (
                                int32_t ret = 0;
                                total + 0x1 < SIZE
                                && (ret = recv(ep->src, buf + total, 0x1, MSG_NOSIGNAL));
                            )
                            {
                                if (ret == -1)
                                    break;
                                else
                                    total += ret;

                                if (strstr(buf, "\r\n\r\n") != NULL) break;
                            }

                            char *header = strdup(buf);
                            char *s_status = NULL;
                            for (
                                char *p = strtok_r(header, "\r\n", &s_status);
                                p != NULL;
                                p = strtok_r(NULL, "\r\n", &s_status)
                            )
                            {
                                uint32_t i = 4;
                                if (strncasecmp("host", p, i) == 0)
                                {
                                    if (p[i + 1] == ' ')
                                        strncpy(url, p + i + 2, LEN_URL);
                                    else
                                        strncpy(url, p + i + 1, LEN_URL);
                                }
                            }
                            free(header);
                            if (*url == 0)
                            {
                                if (sscanf(buf, "CONNECT %" LEN_URL_STR "[^ ] %*[^ ]\r\n", url) != 1) {
                                    if (sscanf(buf, "GET %" LEN_URL_STR "[^ ] %*[^ ]\r\n", url) != 1) {
                                        if (sscanf(buf, "POST %" LEN_URL_STR "[^ ] %*[^ ]\r\n", url) != 1) {
                                            if (LOG) fprintf(stderr, "Unknown connection.\n");
                                            memset(buf, '\0', total);
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
                                    https = true;
                                else
                                {
                                    https = false;
                                    break;
                                }
                            }
                            if (! strchr(url, ':'))
                            {
                                if (https)
                                    strncat(url, ":443", LEN_URL);
                                else
                                    strncat(url, ":80", LEN_URL);
                            }
                        }
                        // Found transparent IP
                        else
                        {
                            if (! inet_ntop(AF_INET, &destination_addr.sin_addr, url, INET_ADDRSTRLEN))
                            {
                                perror("Translation fail");
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                                close(fd);
                                continue;
                            }
                            size_t len = strlen(url);
                            if (len + 7 > LEN_URL)
                            {
                                fprintf(stderr, "URI is too long: %s\n", url);
                                memset(url, '\0', len);
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

                        if ((ep->dst = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
                        {
                            perror("Create socket for zl");
                            memset(buf, '\0', total);
                            memset(url, '\0', strlen(url));
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ep->src, NULL);
                            close(ep->src);
                            continue;
                        }
                        setNonBlocking(ep->dst);
                        set_socket_timeout(ep->dst, 0, TIMEOUT);

                        if (connect(
                            ep->dst,
                            (struct sockaddr *) &server_addr,
                            sizeof(struct sockaddr)) == -1 && errno != EINPROGRESS
                        )
                        {
                            perror("Connect to server fail");
                            close(ep->dst);
                            memset(buf, '\0', total);
                            memset(url, '\0', strlen(url));
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ep->src, NULL);
                            close(ep->src);
                            continue;
                        }
                        snprintf(
                            _buf,
                            SIZE,
                            "CONNECT %s HTTP/1.1\r\n\r\n",
                            url
                        );

                        ep->msg = strdup(_buf);
                        if (! https)
                        {
                            for(
                                int32_t ret = 0;
                                total + READ_SIZE < SIZE
                                && (ret = recv(ep->src, buf + total, READ_SIZE, MSG_NOSIGNAL));
                            )
                            {
                                if (ret == -1)
                                {
                                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                                        break;
                                    else
                                    {
                                        close(ep->dst);
                                        free(ep->msg);
                                        memset(buf, '\0', total);
                                        memset(url, '\0', strlen(url));
                                        memset(_buf, '\0', strlen(_buf));
                                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ep->src, NULL);
                                        close(ep->src);
                                        ep->src = 0;
                                        break;
                                    }
                                }
                                else total += ret;
                            }
                            if (ep->src == 0) continue;
                            ep->http_msg = calloc(sizeof(char), total);
                            memcpy(ep->http_msg, buf, total);
                            ep->length = total;
                        }
                        memset(&event, '\0', sizeof(struct epoll_event));
                        event.events = EPOLLOUT | EPOLLET;
                        ep->state = ESTABLISHING;
                        event.data.ptr = ep;
                        memset(buf, '\0', total);
                        memset(url, '\0', strlen(url));
                        memset(_buf, '\0', strlen(_buf));
                        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ep->dst, &event);
                    }
                    break;
                    case ESTABLISHING:
                    {
                        int32_t ret = recv(ep->dst, buf, RSP_LEN, MSG_NOSIGNAL);
                        if (ret != RSP_LEN || strstr(buf, "\r\n\r\n") == NULL)
                        {
                            memset(buf, '\0', RSP_LEN);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ep->dst, NULL);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ep->src, NULL);
                            close(ep->dst);
                            close(ep->src);
                            if (ep->http_msg)
                            {
                                free(ep->http_msg);
                                ep->http_msg = NULL;
                            }
                            continue;
                        }

                        memset(&event, '\0', sizeof(event));

                        if (ep->http_msg == NULL)
                        {
                            send(ep->src, buf, RSP_LEN, MSG_NOSIGNAL);

                            event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
                            event.data.ptr = ep;

                            ep->state = FORWARDING;
                            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ep->src, &event);

                            struct event_t *_ep = calloc(sizeof(struct event_t), 1);
                            _ep->dst = ep->src;
                            _ep->src = ep->dst;
                            _ep->state = FORWARDING;
                            event.data.ptr = _ep;
                            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, _ep->src, &event);
                        }
                        else
                        {
                            event.events = EPOLLOUT | EPOLLET;
                            ep->state = ESTABLISHING;
                            event.data.ptr = ep;
                            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ep->dst, &event);
                        }
                        memset(buf, '\0', RSP_LEN);
                    }
                    break;
                    case FORWARDING:
                    {
                        int32_t ret = 0;
                        for(
                            ;
                            (ret = recv(ep->src, buf, READ_SIZE, MSG_NOSIGNAL));
                        )
                        {
                            if (ret == -1)
                            {
                                if (errno == EAGAIN || errno == EWOULDBLOCK)
                                    break;
                                else
                                {
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ep->src, NULL);
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ep->dst, NULL);
                                    close(ep->src);
                                    close(ep->dst);
                                    free(ep);
                                    ep = NULL;
                                    break;
                                }
                            }
                            else
                            {
                                send(ep->dst, buf, ret, MSG_NOSIGNAL);
                                memset(buf, '\0', ret);
                            }
                        }

                        if (ret == 0)
                        {
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ep->src, NULL);
                            close(ep->src);
                            free(ep);
                            ep = NULL;
                            continue;
                        }
                    }
                    break;
                }
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) [[unlikely]]
            {
                struct event_t *ep = events[i].data.ptr;

                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ep->src, NULL);
                close(ep->src);
                if (ep->msg)
                {
                    free(ep->msg);
                    ep->msg = NULL;
                }
                if (ep->http_msg)
                {
                    free(ep->http_msg);
                    ep->http_msg = NULL;
                    ep->length = 0;
                }
                free(ep);
                events[i].data.ptr = NULL;
            }
            else if (events[i].events & EPOLLOUT)
            {
                struct event_t *ep = events[i].data.ptr;
                switch (ep->state)
                {
                    case ESTABLISHING:
                    {
                        if (ep->msg)
                        {
                            send(ep->dst, ep->msg, strlen(ep->msg), MSG_NOSIGNAL);
                            free(ep->msg);
                            ep->msg = NULL;

                            ep->state = ESTABLISHING;
                            memset(&event, '\0', sizeof(struct epoll_event));
                            event.events = EPOLLIN | EPOLLET;
                            event.data.ptr = ep;
                            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ep->dst, &event);
                        }
                        else if (ep->http_msg)
                        {
                            for (
                                int32_t ret = 0;
                                (ret = send(ep->dst, ep->http_msg, ep->length, MSG_NOSIGNAL)) > 0
                                && ret != ep->length;
                            );

                            free(ep->http_msg);
                            ep->http_msg = NULL;
                            ep->length = 0;

                            if (ep->state == FORWARDING)
                            {
                                memset(&event, '\0', sizeof(event));
                                event.events = EPOLLIN | EPOLLET;
                                event.data.ptr = ep;
                                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ep->dst, &event);
                            }
                            else
                            {
                                memset(&event, '\0', sizeof(event));
                                event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
                                event.data.ptr = ep;

                                ep->state = FORWARDING;
                                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ep->src, &event);

                                struct event_t *_ep = calloc(sizeof(struct event_t), 1);
                                _ep->dst = ep->src;
                                _ep->src = ep->dst;
                                _ep->state = FORWARDING;
                                event.data.ptr = _ep;
                                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, _ep->src, &event);
                            }
                        }
                    }
                    break;
                }
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
