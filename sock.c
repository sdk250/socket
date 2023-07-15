#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#define SIZE 0x4000
#define TIMEOUT 150000

long int recv_all(int, char*, size_t);
long int send_all(int, const char*, size_t);
int setNonBlocking(int);
static int http = 0;

int main(int argc, char** argv) {
	int server_fd = 0;
	unsigned short int port = 80;
	struct sockaddr_in server_addr = {0}, addr = {0}, client_addr = {0};
	char* data = NULL;
	char* buffer = NULL,*url = NULL;
	socklen_t len = sizeof(struct sockaddr);
	pid_t p = -1;



	// if (argc >= 4) {
	//	 url = argv[1];
	//	 port = atoi(argv[2]);
	// } else
	//	 exit(EXIT_FAILURE);
	
	if ((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("Create server socket file descriptor fail");
		close(server_fd);
		exit(EXIT_FAILURE);
	}
	buffer = (char*) malloc(sizeof(char) * SIZE);
	data = (char*) malloc(sizeof(char) * SIZE);
	url = (char*) malloc(sizeof(char) * SIZE);
	memset(&server_addr, '\0', sizeof(struct sockaddr));
	memset(&addr, '\0', sizeof(struct sockaddr));
	memset(&client_addr, '\0', sizeof(struct sockaddr));
	memset(buffer, '\0', SIZE);
	memset(data, '\0', SIZE);
	memset(url, '\0', SIZE);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(atoi(argv[1]));
	server_addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(server_fd, (const struct sockaddr*)&server_addr, sizeof(struct sockaddr)) < 0) {
		perror("Bind 0.0.0.0:8080 error");
		free(buffer);
		free(data);
		exit(EXIT_FAILURE);
		close(server_fd);
	}
	if (listen(server_fd, 5) < 0) {
		perror("listen");
		free(buffer);
		free(data);
		exit(EXIT_FAILURE);
		close(server_fd);
	}
	printf("Listen on 127.0.0.1:%s.\n", argv[1]);
	for (int client_fd = 0, fd = 0; (client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len)) > 0;) {
		if ((p = fork()) == -1) {
			fprintf(stderr, "%s\n", "Create process error.");
			exit(EXIT_FAILURE);
		} else if (p == 0) {
			memset(data, '\0', SIZE);
			memset(buffer, '\0', SIZE);
			memset(&addr, '\0', sizeof(struct sockaddr));
	
			printf("Accept %s:%u\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
			recv_all(client_fd, data, SIZE);
			char* s = strchr(data, ' ') + 1;
			strncpy(buffer, s, strchr(s, ' ') - s);
			if (strstr(buffer, "http://")) {
				http = 1;
				if ((s = strchr(buffer + 7, ':'))) {
					strncpy(url, buffer + 7, strlen(buffer) - 7);
				} else if ((s = strchr(buffer + 7, '/'))) {
					strncpy(url, buffer + 7, s - buffer - 7);
					strcat(url, ":80");
					strncpy(url + strlen(url), s, strlen(s));
				} else {
					strncpy(url, buffer + 7, strlen(buffer) - 7);
					strcat(url, ":80");
				}
			} else {
				http = 0;
				strncpy(url, s, strchr(s, ' ') - s);
			}
			printf("URL: %s\n", url);
			if (strstr(url, "153.3.236.22")) {
				fprintf(stderr, "%s\n", "\x1b[31mURL ERROR\x1b[0m");
			}
			if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
				perror("Create socket for zl");
				close(client_fd);
				close(fd);
				break;
			}
	
			addr.sin_family = AF_INET;
			addr.sin_port = htons(443);
			addr.sin_addr.s_addr = inet_addr("153.3.236.22");
			if ((connect(fd, (struct sockaddr *)&addr, sizeof(addr))) < 0) {
				perror("Connect zl");
				close(client_fd);
				close(fd);
				break;
			}
			// setNonBlocking(fd);
			memset(buffer, '\0', SIZE);
			strcpy(buffer, data);
			memset(data, '\0', SIZE);
			snprintf(data, SIZE, "CONNECT %sHTTP/1.1\r\nHost: 153.3.236.22\r\nUser-Agent: baiduboxapp\r\nX-T5-Auth: 683556443\r\n\r\n", url);
			send_all(fd, data, strlen(data));
			recv_all(fd, data, SIZE);
			// puts(data);
			if (http) {
				for (long int _recv = -1, _send = -1; _recv != 0 && _send != 0;) {
					_send = send_all(fd, buffer, strlen(buffer));
					// printf("Sended: %ld\n", _send);
					_recv = recv_all(fd, buffer, SIZE);
					// printf("Received: %ld\n", _recv);
					if (!_recv && !_send)
						break;
		
					_send = send_all(client_fd, buffer, _recv);
					// printf("Sended: %ld\n", _send);
					_recv = recv_all(client_fd, buffer, SIZE);
					// printf("Received: %ld\n", _recv);
					if (!_recv && !_send)
						break;
				}
			} else {
				send_all(client_fd, data, strlen(data));
				for (long int _recv = -1, _send = -1; _recv != 0 && _send != 0;) {
					_recv = recv_all(client_fd, data, SIZE);
					// printf("Received: %ld\n", _recv);
					_send = send_all(fd, data, _recv);
					// printf("Sended: %ld\n", _send);
					if (!_recv && !_send)
						break;
		
					_recv = recv_all(fd, data, SIZE);
					// printf("Received: %ld\n", _recv);
					_send = send_all(client_fd, data, _recv);
					// printf("Sended: %ld\n", _send);
					if (!_recv && !_send)
						break;
				}
			}

			close(client_fd);
			close(fd);
			exit(EXIT_SUCCESS);
		} else if (p > 0) {
			continue;
		} else {
			fprintf(stderr, "%s\n", "Unknown error.");
			exit(EXIT_FAILURE);
		}
	}

	close(server_fd);
	free(buffer);
	free(data);
	free(url);
	puts("Done.");

	return 0;
}

long int recv_all(int fd, char* buffer, size_t buffer_size) {
	long int total_recv = 0;
	memset(buffer, '\0', buffer_size);
	for (int i = 0; (i = read(fd, buffer + total_recv, buffer_size - total_recv));) {
		http ? NULL : usleep(TIMEOUT);
		total_recv += i > 0 ? i : 0;
		// if (i == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			// usleep(TIMEOUT);
		// else
			// break;
		if (i != buffer_size - total_recv)
			break;
	}
	return total_recv;
}

long int send_all(int fd, const char* msg, size_t msg_size) {
	long int total_send = 0;
	for (int i = 0; (i = write(fd, msg, msg_size));) {
		// if (i == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			// continue;
		http ? NULL : usleep(TIMEOUT);
		total_send += i > 0 ? i : 0;
		if (total_send == msg_size)
			break;
	}
	return total_send;
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
