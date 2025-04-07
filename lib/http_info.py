from urllib.parse import urlparse
from httptools import HttpRequestParser


class RequestInfo:
    def __init__(self):
        self.scheme: str = ""
        self.domain: str = ""
        self.port: int = 80
        self.url: str = ""
        # =======https(httptools不支持https,自己解析,只需要处理CONNECT连接即可)=======
        self.https_method: str = ""
        self.https_version: str = ""
        # ======================================================================
        self.headers: dict[str, str] = {}
        self.raw_data: bytearray = bytearray()
        self._headers_complete = False
        self._body_complete = False
        self._parser = HttpRequestParser(self)
        pass

    def _feed_data_conncet(self, data: bytes):
        req_str = data.decode()
        # method url version
        # headers
        sps = req_str.split()
        self.scheme = "https"
        self.https_method = sps[0]
        self.url = sps[1]
        host_port = self.url.split(":")
        self.domain = host_port[0]
        self.port = int(host_port[1])
        self.https_version = sps[2].split("/")[1]

    def feed_data(self, data: bytes):
        self.raw_data.extend(data)
        try:
            self._parser.feed_data(data)
        except Exception as e:
            self._feed_data_conncet(data)

    def get_method(self) -> str:
        return (
            self.https_method if self.is_https() else self._parser.get_method().decode()
        )

    def get_http_version(self) -> str:
        return (
            self.https_version if self.is_https() else self._parser.get_http_version()
        )

    def is_https(self) -> bool:
        return self.scheme == "https"

    def is_complete(self) -> bool:
        return self._headers_complete and self._body_complete

    def clear(self):
        """强制重置实例"""
        self.__dict__.clear()  # 清空所有成员变量
        self.__init__()  # 重新初始化

    # 以下为 httptools 回调函数
    def on_url(self, url: bytes):
        self.url = url.decode()
        parsed_url = urlparse(self.url)
        self.scheme = parsed_url.scheme
        self.domain = parsed_url.netloc
        if parsed_url.port is None:
            self.port = 443 if self.is_https() else 80
        else:
            self.port = parsed_url.port

    def on_header(self, name: bytes, value: bytes):
        self.headers[name.decode()] = value.decode()
        pass

    def on_headers_complete(self):
        self._headers_complete = True

    def on_body(self, body: bytes):
        pass

    def on_message_complete(self):
        self._body_complete = True
