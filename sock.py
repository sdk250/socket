import socket

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
    sock.connect(("127.0.0.1", 8080))
    sock.send(b"GET / HTTP/1.1\r\nUser-Agent: C/Socket\r\n\r\n")
    print(sock.recv(100))
    sock.send(b"GET /get HTTP/1.1\r\nHost: 54.204.94.184\r\nConnection: Keep-Alive\r\n\r\n")
    print(sock.recv(0x1FF).decode())
