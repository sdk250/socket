import socket
import ssl

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
    sock.connect(("153.3.236.22", 443))
    sock.send(b"CONNECT httpbin.org:80/get/getHTTP/1.1\r\nHost: 153.3.236.22\r\nUser-Agent: baiduboxapp\r\nX-T5-Auth: 683556443\r\nProxy-Connection: Keep-Alive\r\nConnection: Keep-Alive\r\n\r\n")
    print(sock.recv(100))
    sock.send(b"GET /get HTTP/1.1\r\nHost: httpbin.org\r\nConnection: Keep-Alive\r\n\r\n")
    print(sock.recv(0x1FF).decode())
