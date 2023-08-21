thread_socket:
	gcc -m64 -O3 -Wall -lpthread -o thread_socket thread_socket.c driver.c

clean:
	rm ./thread_socket

install:
	cp -f ./thread_socket /usr/bin 

uninstall:
	rm -f /usr/bin/thread_socket
