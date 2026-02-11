#include <getopt.h>

#include "thread_socket.h"

int32_t LOG = 0;
int32_t LOCAL_FD = 0;
struct sockaddr_in server_addr = { 0 };
atomic_bool SHUTDOWN = false;

int main(int argc, char *const *argv)
{
    struct sockaddr_in local_addr = { 0 };
    int opt = 0;
    int daemon = 0;
    unsigned int port = 8080;
    pid_t pid = 0;
    pid_t sid = 0;
    char ip_s[16] = { 0 };

    if (argc == 1)
        usage(*argv, EXIT_FAILURE);
    for (; (opt = getopt(argc, argv, "p:u:r:dlh")) != -1; )
    {
        switch(opt)
        {
            case 'p':
                port = atoi(optarg);
                break;
            case 'd':
                daemon = 1;
                break;
            case 'l':
                LOG = 1;
                break;
            case 'u':
                if (setuid(atoi(optarg)))
                    perror("Setuid error");
                break;
            case 'r':
                strncpy(ip_s, optarg, 15);
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
            default:
                usage(*argv, EXIT_FAILURE);
        }
    }

    // 未指定 Server 时，使用默认
    if (*ip_s == '\0')
        strncpy(ip_s, SERVER_ADDR, 15);

    memset(&server_addr, '\0', sizeof(struct sockaddr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(443);
    if (inet_pton(AF_INET, ip_s, &server_addr.sin_addr) != 1)
    {
        perror("Translation address failed");
        exit(EXIT_FAILURE);
    }

    // 捕获发送中断、程序停止等信号
    signal(SIGPIPE, SIG_IGN);
    signal(SIGALRM, SIG_IGN);
    signal(SIGINT, signal_terminate);
    signal(SIGTERM, signal_terminate);

    if ((LOCAL_FD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        perror("Failed to create local server socket file descriptor");
        close(LOCAL_FD);
        exit(EXIT_FAILURE);
    }

    int on = 1;
    // 端口复用
    setsockopt(LOCAL_FD, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    // 接收来自 TPROXY 的流量
    setsockopt(LOCAL_FD, SOL_IP, IP_TRANSPARENT, &on, sizeof(on));

    memset(&local_addr, '\0', sizeof(struct sockaddr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port);
    local_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(
        LOCAL_FD,
        (const struct sockaddr *) &local_addr,
        sizeof(struct sockaddr)
    ) < 0)
    {
        fprintf(
            stderr,
            "Bind %s:%u error",
            inet_ntoa(local_addr.sin_addr),
            port
        );
        perror(" ");
        close(LOCAL_FD);
        exit(EXIT_FAILURE);
    }
    if (listen(LOCAL_FD, MAX_EVENT) < 0)
    {
        perror("listen");
        close(LOCAL_FD);
        exit(EXIT_FAILURE);
    }
    if (setNonBlocking(LOCAL_FD) == -1)
    {
        perror("Set nonblocking socket");
        close(LOCAL_FD);
        exit(EXIT_FAILURE);
    }
    printf(
        __TIME__ "\t" __DATE__ "\n"
        "Listen on %s:%u.\n",
        inet_ntoa(local_addr.sin_addr),
        port
    );

    if (daemon) {
        if ((pid = fork()) == 0) {
            if ((sid = setsid()) < 0) {
                perror("setsid error");
                exit(EXIT_FAILURE);
            }
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            // close(STDERR_FILENO);
            main_loop(LOCAL_FD);
        } else {
            printf("The PID of %s is %d.\n", *argv, pid);
            close(LOCAL_FD);
        }
    } else
        main_loop(LOCAL_FD);

    return 0;
}
