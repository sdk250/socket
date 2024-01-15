CFLAGS = -m64 -O3 -Wall -lpthread -fPIE -fstack-protector -Wno-incompatible-pointer-types

thread_socket:
	clang $(CFLAGS) -o $(@) thread_socket.c driver.c

clean:
	rm ./thread_socket

install:
	cp -f ./thread_socket /usr/bin 

uninstall:
	rm -f /usr/bin/thread_socket
