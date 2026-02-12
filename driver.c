#include <sys/time.h>

#include "thread_socket.h"
#include "sniff_host.h"

const uint8_t RSP_LEN = 39;

void signal_terminate(const int32_t sign)
{
    atomic_store_explicit(&SHUTDOWN, true, memory_order_release);
    fprintf(stderr, "Receive terminated signal: %d\n", sign);
    close(LOCAL_FD);
}

void usage(const char *argv, const int32_t ret)
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

/// @brief 从本地监听的套接字中接入一个连接
/// @param epoll_fd EPOLL FD
/// @param local_fd 本地监听的套接字
/// @return 非 0 值为失败
int32_t accept_conn(const int32_t epoll_fd, const int32_t local_fd)
{
    struct Session *session = calloc(1, sizeof(struct Session));
    if (session == NULL) return -1;
    struct Context *local = session->objs;
    struct Context *remote = session->objs + 1;
    local->src = local->dst =
        remote->src = remote->dst = -1;
    local->session = remote->session = session;
    local->peer = remote;
    remote->peer = local;
    local->buf = calloc(BUFSIZE, sizeof(uint8_t));
    remote->buf = calloc(BUFSIZE, sizeof(uint8_t));
    if (rb_init(&local->rb, BUFSIZE) != 0) goto failure;
    if (rb_init(&remote->rb, BUFSIZE) != 0) goto failure;
    if (! local->buf || ! remote->buf) goto failure;

    // 接入新连接
    if ((local->src = accept(local_fd, NULL, NULL)) < 0) goto failure;
    if (setNonBlocking(local->src) < 0) goto failure;

    session->state = WAIT_ESTABLISH;
    remote->src = local->dst;
    remote->dst = local->src;

    struct epoll_event event;

    memset(&event, '\0', sizeof(event));
    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = local;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, local->src, &event) != 0) goto failure;

    return 0;

    failure:
        if (local->src >= 0) close(local->src);
        free(local->buf);
        free(remote->buf);
        free(session);
        return -1;
}

/// @brief 判断一个 32位无符号地址 是否为本地链路地址
/// @param s_addr 无符号地址
/// @return Boolean
int32_t is_internal_ip(uint32_t s_addr)
{
    return (ntohl(s_addr) == 0x0) || // 0.0.0.0/0
    (ntohl(s_addr) & 0xff000000) == 0x0a000000 || // 10.0.0.0/8
    (ntohl(s_addr) & 0xfff00000) == 0xac100000 || // 172.16.0.0/12
    (ntohl(s_addr) & 0xffff0000) == 0xc0a80000 || // 192.168.0.0/16
    (ntohl(s_addr) & 0xff000000) == 0x7f000000 || // 127.0.0.0/8
    (ntohl(s_addr) & 0xffff0000) == 0xa9fe0000 || // 169.254.0.0/16
    (ntohl(s_addr) & 0xf0000000) == 0xe0000000 // 224.0.0.0/4
    ;
}

/// @brief 嗅探来自client的报文，分析出需要连接的目标地址
/// @param ctx Context
/// @return 非 0 值为失败
int32_t sniff_host(struct Context *ctx)
{
    struct sockaddr_in destination_addr;
    socklen_t sock_len = sizeof(struct sockaddr);
    memset(&destination_addr, '\0', sock_len);

    if (getsockopt(
        ctx->src, SOL_IP, SO_ORIGINAL_DST,
        &destination_addr, &sock_len
    ) != 0 ||
        is_internal_ip(destination_addr.sin_addr.s_addr)
    )
    // TPROXY 嗅探失败或嗅探到了本地地址
    {
        int32_t n = recv(ctx->src, ctx->buf, BUFSIZE, MSG_NOSIGNAL);
        if (n > 0) ctx->data_len += n;
        else if (n < 0 && (errno != EAGAIN || errno != EWOULDBLOCK)) return -1;
        else return -1;

        int32_t ret = 0;
        if ((ret = sniff(ctx->buf, ctx->data_len, &ctx->session->host)) != 0)
        {
            fprintf(stderr, "Failed to parse host, ret: %d", ret);
            return ret;
        }
    }
    else
    // 成功通过 TPROXY 嗅探到了目的地址
    {
        uint8_t buf[256] = { 0 };
        if (! inet_ntop(AF_INET, &destination_addr.sin_addr, (char *) buf, INET_ADDRSTRLEN))
        {
            return -1;
        }
        size_t len = strlen((const char *) buf);
        if (len + 7 > MAX_HOST_LEN)
        {
            fprintf(stderr, "URI is too long: %s\n", buf);
            return -1;
        }
        memcpy(ctx->session->host.host, buf, sizeof(ctx->session->host.host));
        ctx->session->host.port_num = ntohs(destination_addr.sin_port);
    }

    return 0;
}

void failure(struct Context **ctx_p)
{
    struct Context *ctx = *ctx_p;
    if (ctx == NULL) return;
    if (ctx->src >= 0) close(ctx->src);
    if (ctx->dst >= 0) close(ctx->dst);
    ctx->src = ctx->dst = ctx->peer->src = ctx->peer->dst = -1;
    struct Session *tmp = ctx->session;
    free(ctx->peer->buf);
    free(ctx->buf);
    ctx->session = NULL;
    ctx->buf = ctx->peer->buf = NULL;
    rb_free(&ctx->rb);
    rb_free(&ctx->peer->rb);
    free(tmp);
    *ctx_p = NULL;
}

int32_t epoll_mod(int32_t epoll_fd, const struct Context *ctx, uint32_t events)
{
    struct epoll_event event = { 0 };
    event.events = events | EPOLLET;
    event.data.ptr = (void *) ctx;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ctx->src, &event);
}

void event_in(struct Context **ctx_p, int32_t epoll_fd)
{
    struct Context *ctx = *ctx_p;
    struct epoll_event event;
    switch (ctx->session->state)
    {
        case WAIT_ESTABLISH:
        {
            if (sniff_host(ctx) != 0)
            {
                failure(ctx_p);
                break;
            }
            if (LOG)
                printf(
                    "url: %s:%d\n",
                    ctx->session->host.host, ctx->session->host.port_num
                );

            // 连接远程 server
            if ((ctx->dst = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) failure(ctx_p);
            if (setNonBlocking(ctx->dst) < 0) failure(ctx_p);
            set_socket_timeout(ctx->dst, 0, TIMEOUT);
            if (
                connect(ctx->dst, (struct sockaddr *) &server_addr, sizeof(struct sockaddr)) == -1
                && errno != EINPROGRESS
            ) failure(ctx_p);

            ctx->peer->src = ctx->dst;
            memset(&event, '\0', sizeof(event));
            event.events = EPOLLOUT | EPOLLET;
            event.data.ptr = ctx->peer;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ctx->peer->src, &event) != 0) failure(ctx_p);
            if (epoll_mod(epoll_fd, ctx, 0) != 0) failure(ctx_p);
        }
        break;

        case ESTABLISHING:
        {
            int32_t ret = recv(ctx->src, ctx->buf, BUFSIZE, MSG_NOSIGNAL);
            if (strncmp(
                "HTTP/1.1 200 Connection established\r\n\r\n",
                (const char *) ctx->buf, RSP_LEN
            ) != 0)
            {
                failure(ctx_p);
                break;
            }
            if (ctx->session->host.proto == PROTO_HTTPS)
            {
                rb_append(&ctx->rb, ctx->buf, ret);
                if (epoll_mod(epoll_fd, ctx->peer, EPOLLOUT) != 0) failure(ctx_p);
            } else {
                rb_append(&ctx->peer->rb, ctx->peer->buf, ctx->data_len);
                if (epoll_mod(epoll_fd, ctx, EPOLLOUT) != 0) failure(ctx_p);
            }
            ctx->session->state = FORWARDING;

            memset(ctx->buf, '\0', ctx->data_len);
            ctx->data_len = 0;
        }
        break;

        case FORWARDING:
        {
            int32_t ret = 0;
            size_t space = rb_space(&ctx->rb);
            for (;
                ! rb_is_full(&ctx->rb)
                && (ret = recv(ctx->src, ctx->buf, space < BUFSIZE ? space : BUFSIZE, MSG_NOSIGNAL));
                space = rb_space(&ctx->rb)
            )
            {
                if (ret > 0)
                {
                    if (rb_append(&ctx->rb, ctx->buf, ret) != ret)
                    {
                        fprintf(stderr, "RB: capacity=%zu, size=%zu, tail=%zu, head=%zu\n", ctx->rb.capacity, ctx->rb.size, ctx->rb.tail, ctx->rb.head);
                        failure(ctx_p);
                        return;
                    }
                    ctx->data_len += ret;
                    int32_t written = rb_flush(&ctx->rb, ctx->dst);
                    if (written > 0) ctx->data_len -= written;
                    else if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                    else
                    {
                        failure(ctx_p);
                        return;
                    }
                }
                else if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                else
                {
                    perror("Received");
                    failure(ctx_p);
                    return;
                }
            }

            if (! rb_is_empty(&ctx->rb))
            {
                if (epoll_mod(epoll_fd, ctx, 0) != 0) failure(ctx_p);
                if (epoll_mod(epoll_fd, ctx->peer, EPOLLOUT) != 0) failure(ctx_p);
                return;
            }
            if (ret == 0) failure(ctx_p);
        }
        break;

        case UNKNOWN:
        {
            failure(ctx_p);
            break;
        }
    }
}

void event_out(struct Context **ctx_p, int32_t epoll_fd)
{
    struct Context *ctx = *ctx_p;
    switch (ctx->session->state)
    {
        case WAIT_ESTABLISH:
        {
            int32_t len = snprintf(
                (char *) ctx->buf, BUFSIZE,
                "CONNECT %s:%u HTTP/1.1\r\n\r\n",
                (const char *) ctx->session->host.host,
                ctx->session->host.port_num
            );
            rb_append(&ctx->peer->rb, ctx->buf, len);
            ctx->data_len = ctx->peer->data_len;
            ctx->session->state = ESTABLISHING;
        }

        case FORWARDING:
        case ESTABLISHING:
        {
            struct Context *peer = ctx->peer;
            for (; ! rb_is_empty(&peer->rb); )
            {
                int32_t ret = rb_flush(&peer->rb, peer->dst);
                if (ret > 0) peer->data_len -= ret;
                else if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
                else
                {
                    failure(ctx_p);
                    return;
                }
            }
            if (epoll_mod(epoll_fd, ctx, EPOLLIN) != 0) failure(ctx_p);
            if (epoll_mod(epoll_fd, peer, EPOLLIN) != 0) failure(ctx_p);
        }
        break;

        case UNKNOWN:
        {
            failure(ctx_p);
            break;
        }
    }
}

void main_loop(const int32_t local_fd)
{
    int32_t epoll_fd = 0, event_count = 0;
    struct epoll_event event, events[MAX_EVENT];

    epoll_fd = epoll_create1(0);

    event.events = EPOLLIN;
    event.data.fd = local_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, local_fd, &event);
    for (; ! atomic_load_explicit(&SHUTDOWN, memory_order_acquire); )
    {
        event_count = epoll_wait(epoll_fd, events, MAX_EVENT, -1);
        for (int32_t i = 0; i < event_count; i++)
        {
            int32_t fd = events[i].data.fd;
            struct Context *ctx = events[i].data.ptr;
            if (fd == local_fd)
            {
                if (accept_conn(epoll_fd, fd) == -1) perror("Accept");
                continue;
            }

            if (events[i].events & EPOLLIN) event_in(&ctx, epoll_fd);

            if (ctx != NULL && events[i].events & EPOLLOUT) event_out(&ctx, epoll_fd);
        }
    }

    close(epoll_fd);
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

int32_t setNonBlocking(int32_t sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) return flags;
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}
