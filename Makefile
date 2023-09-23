CFLAGS = -m64 -O3 -Wall -lpthread -fPIE -fstack-protector-all -Wno-incompatible-pointer-types

thread_socket:
	gcc $(CFLAGS) -o $(@) thread_socket.c driver.c

clean:
	rm ./thread_socket

install:
	cp -f ./thread_socket /usr/bin 

uninstall:
	rm -f /usr/bin/thread_socket
