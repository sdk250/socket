#include "thread_socket.h"

void signal_terminate(int sign) {
	close(local_fd);
	exit(EXIT_SUCCESS);
}

void usage(const char *argv, int ret) {
	printf("Usage of %s:\n\t-p\t<PORT>\n\t\tSet PORT while running\n\t-l\tShow running log\n\t-u\t<UID>\n\t\tSet UID while running\n\t-d\tStart daemon service\n\t-h\tShow this message\n", argv);
	exit(ret);
}

void main_loop(int local_fd) {
	for (socklen_t len = sizeof(struct sockaddr); ;) {
		pthread_t tid = 0;
		int *client_fd = (int *) malloc(sizeof(int));
		*client_fd = accept(local_fd, (struct sockaddr *) NULL, &len);
		pthread_create(&tid, &attr, handle_connection, client_fd);
		pthread_detach(tid);
	}
}

void set_socket_timeout(int fd, unsigned long int usec, unsigned int sec) {
	struct timeval tv = {usec, sec};
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (void *) &tv, sizeof(struct timeval));
	tv.tv_usec = usec;
	tv.tv_sec = sec;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (void *) &tv, sizeof(tv));
}

void add_header(struct http_header* root, const char* header, size_t len) {
	struct http_header *temp = root->next;
	root->next = (struct http_header *) malloc(sizeof(struct http_header));
	temp->prev = root->next;
	root->next->data = (char *) malloc(sizeof(char) * len);
	memset(root->next->data, '\0', len);
	strcpy(root->next->data, header);
	root->next->prev = root;
	root->next->next = temp;
}

void *handle_connection(void *_fd) {
	int server_fd;
	int client_fd = *(int *)_fd;
	char *buffer = NULL, *_buffer = NULL, *url = NULL;
	char method[10] = {0}, http_version[10] = {0};
	struct sockaddr_in server_addr = {0};
	pthread_t t1 = 0, t2 = 0;
	int http = -1;
	struct http_header *root = NULL;

	set_socket_timeout(client_fd, 0, TIMEOUT);
	free((int *) _fd);

	buffer = (char *) malloc(sizeof(char) * SIZE);
	_buffer = (char *) malloc(sizeof(char) * SIZE);
	url = (char *) malloc(sizeof(char) * 0x100);
	root = (struct http_header *) malloc(sizeof(struct http_header));

	if (!buffer || !_buffer || !url || !root)
		goto exit_label;

	memset(&server_addr, '\0', sizeof(struct sockaddr));
	memset(buffer, '\0', SIZE);
	memset(_buffer, '\0', SIZE);
	memset(url, '\0', 0x100);
	root->data = root->prev = root->next = NULL;

	recv_headers(client_fd, root);

	if (root->data && sscanf(root->data, "%9[^ ] %*[^ ] %9[^ ]\r\n", method, http_version) != 2)
		goto exit_label;
	if (strcmp(method, "CONNECT") == 0)
		http = 0;
	else {
		if (strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0)
			http = 1;
		else
			goto exit_label;
	}

	if (http) {
		for (struct http_header *temp = root; temp; temp = temp->next) {
			if (strstr(temp->data, "Host: ")) {
				memcpy(url, temp->data + 6, strchr(temp->data, '\r') - (temp->data + 6));
				if (!strchr(url, ':'))
					strcat(url, ":80");
				break;
			} else if (temp->next == NULL)
				goto exit_label;
		}
	} else {
		for (struct http_header *temp = root; temp; temp = temp->next) {
			if (strstr(temp->data, "Host: ")) {
				memcpy(url, temp->data + 6, strchr(temp->data, '\r') - (temp->data + 6));
				if (!strchr(url, ':'))
					strcat(url, ":443");
				break;
			} else if (temp->next == NULL)
				goto exit_label;
		}
	}

	if (LOG)
		printf("URL: %s\n", url);

	if (strstr(url, SERVER_ADDR))
		fprintf(stderr, "%s\n", "\x1b[31mURL ERROR\x1b[0m");
	if ((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("Create socket for zl");
		close(server_fd);
		goto exit_label;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(443);
	server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);
	if ((connect(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr))) < 0) {
		perror("Connect zl");
		close(server_fd);
		goto exit_label;
	}
	sprintf(_buffer, "CONNECT %s %s\r\n\r\n", url, http_version);
	send(server_fd, _buffer, strlen(_buffer), MSG_NOSIGNAL);
	memset(_buffer, '\0', SIZE);
	recv(server_fd, _buffer, 39, 0);
	if (http)
		for (struct http_header* s = root; s; s = s->next)
			send(server_fd, s->data, strlen(s->data), MSG_NOSIGNAL);

	struct arg arg_ = {client_fd, server_fd, buffer, http};
	pthread_create(&t1, &attr, client_to_server, &arg_);
	struct arg arg2 = {server_fd, client_fd, _buffer, http};
	pthread_create(&t2, &attr, server_to_client, &arg2);

	pthread_join(t1, NULL);
	pthread_join(t2, NULL);
	close(server_fd);

exit_label:
	for (struct http_header *s = root; s;) {
		struct http_header *temp = s;
		free(s->data);
		s = s->next;
		free(temp);
	}
	close(client_fd);
	free(buffer);
	free(_buffer);
	free(url);
	pthread_exit(NULL);
	return NULL;
}

void *client_to_server(void *par) {
	struct arg *arg_ = (struct arg *) par;
	memset(arg_->buf, '\0', SIZE);

	for (long int n = 0; (n = recv(arg_->source, arg_->buf, SIZE, 0));) {
		if (n == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			else
				break;
		}
		send(arg_->dest, arg_->buf, n, MSG_NOSIGNAL);
	}
	shutdown(arg_->dest, SHUT_RDWR);
	shutdown(arg_->source, SHUT_RDWR);
	pthread_exit(NULL);
	return NULL;
}

void *server_to_client(void *par) {
	struct arg* arg_ = (struct arg*) par;
	if (!arg_->http) {
		if (!strcmp(arg_->buf, "HTTP/1.1 200 Connection established\r\n\r\n"))
			send(arg_->dest, arg_->buf, strlen(arg_->buf), MSG_NOSIGNAL);
	}
	memset(arg_->buf, '\0', SIZE);

	for (long int n = 0; (n = recv(arg_->source, arg_->buf, SIZE, 0)) > 0;) {
		if (n == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			else
				break;
		}
		send(arg_->dest, arg_->buf, n, MSG_NOSIGNAL);
	}
	shutdown(arg_->dest, SHUT_RDWR);
	shutdown(arg_->source, SHUT_RDWR);
	pthread_exit(NULL);
	return NULL;
}

long int recv_headers(int fd, struct http_header *headers) {
	long int ret = 0, total = 0;
	char buffer[SIZE] = {0};
	struct http_header *cur = headers;
	for (char ch = 0; total <= SIZE; ch = 0) {
		ret = recv(fd, &ch, 1, 0);
		if (ret == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			else
				break;
		} else if (ret == 0)
			break;
		else
			*(buffer + total++) = ch;
		if (strstr(buffer, "\r\n")) {
			if (cur->prev == NULL && cur->data == NULL) {
				cur->data = (char *) malloc(sizeof(char) * total + 1);
				if (cur->data == NULL)
					break;
				memset(cur->data, '\0', total + 1);
				memcpy(cur->data, buffer, total);
			} else {
				cur->next = (struct http_header *) malloc(sizeof(struct http_header));
				if (cur->next == NULL)
					break;
				cur->next->data = (char *) malloc(sizeof(char) * total + 1);
				if (cur->next->data == NULL)
					break;
				memset(cur->next->data, '\0', total + 1);
				memcpy(cur->next->data, buffer, total);
				cur->next->prev = cur;
				cur->next->next = NULL;
				cur = cur->next;
			}
			if (strcmp(buffer, "\r\n") == 0)
				break;
			memset(buffer, '\0', SIZE);
			total = 0;
		}
	}
	return total;
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
