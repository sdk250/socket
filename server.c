#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#define SIZE 0x2FF

int main(void) {
	int fd = 0;
	struct sockaddr_in addr = {0}, client = {0};
	socklen_t len = sizeof(struct sockaddr);
	char* buffer = (char*) malloc(sizeof(char) * SIZE);

	fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	printf("Create socket file descriptor: %d\n", fd);
	memset(&addr, '\0', sizeof(struct sockaddr));
	memset(&client, '\0', sizeof(struct sockaddr));
	memset(buffer, '\0', SIZE);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(8080);
	printf("Bind function return: %d\n", bind(fd, (struct sockaddr*)&addr, sizeof(struct sockaddr)));
	printf("Listen function return: %d\n", listen(fd, 5));
	int i = 0;
	i = accept(fd, (struct sockaddr*)&client, &len);
	printf("Accept function return: %d\nAddress: %s:%u\n", i, inet_ntoa(client.sin_addr), ntohs(client.sin_port));
	char* response = "HTTP/1.1 200 OK\r\nServer: C/Socket\r\n\r\n<h1><b><i>Hello</i></b></h1>";
	printf("Read: %zd bytes.\n", read(i, buffer, SIZE));
	printf("Sended: %zd bytes\n", write(i, response, strlen(response)));

	puts(buffer);

	close(fd);
	free(buffer);
	return 0;
}
