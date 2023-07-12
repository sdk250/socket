#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#define SIZE 0x4F0

long int process(int, const char*, char*);
int setNonBlocking(int);

int main(int argc, char** argv) {
	int fd = 0;
	int server_fd = 0;
	unsigned short int port = 80;
	struct sockaddr_in addr = {0}, server_addr = {0};
	char* url = NULL;
	char* data = NULL;
	char* buffer = NULL;

	if (argc >= 4) {
		url = argv[1];
		port = atoi(argv[2]);
	} else
		exit(EXIT_FAILURE);
	buffer = (char*) malloc(sizeof(char) * SIZE);
	data = (char*) malloc(sizeof(char) * SIZE);
	snprintf(data, SIZE, "CONNECT %s:%uHTTP/1.1\r\nHost: 153.3.236.22\r\nUser-Agent: baiduboxapp\r\nX-T5-Auth: 683556443\r\nProxy-Connection: Keep-Alive\r\nConnection: Keep-Alive\r\n\r\n", url, port);
	fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	printf("Create socket file object: %d\n", fd);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(443);
	printf("%d\n", inet_pton(AF_INET, "153.3.236.22", &(addr.sin_addr.s_addr)));
	// server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	// printf("Create socket file object of server: %d\n", server_fd);
	// server_addr.sin_family = AF_INET;
	// server_addr.sin_port = htons(8080);
	// printf("Server address return: %d\n", inet_pton(AF_INET, ""

	printf("Connect return: %d\n", connect(fd, (struct sockaddr *)&addr, sizeof(addr)));
	printf("Return: %d\n", setNonBlocking(fd));
	printf("Received: %ld\n", process(fd, data, buffer));
	puts(buffer);
	memset(data, '\0', SIZE);
	memset(buffer, '\0', SIZE);
	for (int i = 3; i < argc; i++) {
		strcat(data, argv[i]);
		strcat(data, (i == argc - 1) ? "\r\n\r\n" : "\r\n");
	}
	printf("Received: %ld\n", process(fd, data, buffer));
	puts(buffer);
	// memset(data, '\0', SIZE);
	// memset(buffer, '\0', SIZE);
	// strcpy(data, "GET /get?a=0 HTTP/1.1\r\nHost: 54.165.93.75\r\nConnection: Keep-Alive\r\n\r\n");
	// printf("Received: %ld\n", process(fd, data, buffer));
	// puts(buffer);
	// memset(data, '\0', SIZE);
	// memset(buffer, '\0', SIZE);
	// strcpy(data, "POST /post HTTP/1.1\r\nHost: 54.165.93.75\r\nAccept: application/json\r\nConnection: Close\r\n\r\n{\"a\":0}");
	// printf("Received: %ld\n", process(fd, data, buffer));
	// puts(buffer);
	close(fd);
	free(buffer);
	free(data);
	puts("Done.");

	return 0;
}

long int process(int fd, const char* msg, char* buffer) {
	puts(msg);
	long int total_recv = 0;
	long int total_send = 0;
	for (
		int i = 0;
		(i = write(fd, msg, strlen(msg)));
		total_send += (i > 0) ? i : 0
	) {
		if (i == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			sleep(1);
		else
			break;
	}
	printf("Sended: %ld\n", total_send);
	for (
		int i = 0;
		(i = read(fd, buffer + total_recv, SIZE - total_recv));
		total_recv += (i > 0) ? i : 0
	) {
		if (i == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			sleep(1);
		} else
			break;
	}
	*(buffer + total_recv - 1) = '\0';
	return total_recv;
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
