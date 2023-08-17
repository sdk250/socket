thread_socket:
	clang -m64 -O3 -Wall -lpthread -o thread_socket thread_socket.c

clean:
	rm ./thread_socket

install:
	cp -f ./thread_socket /usr/bin 

uninstall:
	rm -f /usr/bin/thread_socket
