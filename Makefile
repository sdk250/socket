CFLAGS = -m64 -O3 -Wall -lc -fPIE

thread_socket:
	clang $(CFLAGS) -o $(@) thread_socket.c driver.c sniff_host.c ring_buffer.c

clean:
	rm ./thread_socket

install:
	cp -f ./thread_socket /usr/bin 

uninstall:
	rm -f /usr/bin/thread_socket
