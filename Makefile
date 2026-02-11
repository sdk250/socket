CFLAGS = -m64 -O3 -Wall -lc -fPIE
NAME = esocket

esocket:
	clang $(CFLAGS) -o $(@) thread_socket.c driver.c sniff_host.c ring_buffer.c

clean:
	rm ./$(NAME)

install:
	cp -f ./$(NAME) /usr/bin 

uninstall:
	rm -f /usr/bin/$(NAME)
