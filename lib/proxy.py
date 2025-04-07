from dataclasses import dataclass
from typing import Awaitable, Callable
from trio import SocketStream
import trio
from loguru import logger

from .http_info import RequestInfo


async def forward_data(
    source: SocketStream,
    dest: SocketStream,
):
    """双向转发数据"""
    try:
        while True:
            data = await source.receive_some(4096)
            if len(data) == 0:  # todo: 暂时不能及时判断需要跟代理服务器断开连接
                # logger.info("[{}] 数据为空", source.socket.getpeername())
                break
            await dest.send_all(data)
    except Exception as e:
        logger.warning("[{}] {}", source.socket.getpeername(), e)
        pass


async def recv_request(
    client: SocketStream,
    cb: Callable[[SocketStream, RequestInfo], Awaitable[None]],
    timeout_sec: float = 0.5,
    once: bool = True,
):
    req_info = RequestInfo()
    while True:
        data = bytearray()
        with trio.move_on_after(timeout_sec) as cancel_scope:
            data = await client.receive_some(4096)
        if len(data) == 0 or cancel_scope.cancelled_caught:
            break
        req_info.feed_data(data)
        if req_info.is_complete():
            await cb(client, req_info)
            if once:
                break
            req_info.clear()


async def send_connect(
    proxy_stream: SocketStream, target_host: str, target_port: int
) -> str:
    # 发送裸 CONNECT 请求（无 Host 和 User-Agent）
    connect_request = f"CONNECT {target_host}:{target_port} HTTP/1.1\r\n\r\n"
    await proxy_stream.send_all(connect_request.encode())
    # 读取代理响应
    response = await proxy_stream.receive_some(4096)
    return response


async def handle_http(
    client_stream: SocketStream,
    proxy_stream: SocketStream,
    request: bytearray,
    response: bytearray,
):
    """处理 HTTP 请求"""
    await proxy_stream.send_all(request)
    await forward_data(proxy_stream, client_stream)


async def handle_https(
    client_stream: SocketStream,
    proxy_stream: SocketStream,
    request: bytearray,
    response: bytearray,
):
    """处理 HTTPS 请求"""
    # 转发给客户端
    await client_stream.send_all(response)
    # 双向隧道转发
    async with trio.open_nursery() as nursery:
        nursery.start_soon(forward_data, client_stream, proxy_stream)
        nursery.start_soon(forward_data, proxy_stream, client_stream)


async def handler_request(client: SocketStream, req_info: RequestInfo):
    target_host = req_info.domain
    target_port = req_info.port

    logger.info(
        "{client} {method} {url} {version} Content-Length:{content_length}",
        client=client.socket.getpeername(),
        method=req_info.get_method(),
        url=req_info.url,
        version=req_info.get_http_version(),
        content_length=req_info.headers.get("Content-Length"),
    )

    # 连接到百度代理池
    proxy_stream = await trio.open_tcp_stream("cloudnproxy.baidu.com", 443)
    response = await send_connect(proxy_stream, target_host, target_port)
    if req_info.is_https():
        await handle_https(client, proxy_stream, req_info.raw_data, response)
    else:
        await handle_http(client, proxy_stream, req_info.raw_data, response)


@dataclass
class BaiduProxyServer:
    listen_addr: str = "0.0.0.0"
    listen_port: int = 8080
    baidu_proxy_host: str = "cloudnproxy.baidu.com"
    baidu_proxy_port: int = 443
    open_log: bool = True

    async def handler_request(self, client: SocketStream, req_info: RequestInfo):
        target_host = req_info.domain
        target_port = req_info.port
        if self.open_log:
            logger.info(
                "{client} {method} {url} {version} Content-Length:{content_length}",
                client=client.socket.getpeername(),
                method=req_info.get_method(),
                url=req_info.url,
                version=req_info.get_http_version(),
                content_length=req_info.headers.get("Content-Length"),
            )

        # 连接到百度代理池
        proxy_stream = await trio.open_tcp_stream(
            self.baidu_proxy_host, self.baidu_proxy_port
        )
        response = await send_connect(proxy_stream, target_host, target_port)
        if req_info.is_https():
            await handle_https(client, proxy_stream, req_info.raw_data, response)
        else:
            await handle_http(client, proxy_stream, req_info.raw_data, response)

    async def handle_client(self, client_stream: SocketStream):
        """处理客户端连接"""
        try:
            await recv_request(client_stream, self.handler_request)
        except Exception as e:
            logger.warning("[{}] {}", client_stream.socket.getpeername(), e)
        finally:
            await client_stream.aclose()

    async def _serve_tcp_forward(self):
        await trio.serve_tcp(
            self.handle_client,
            port=self.listen_port,
            host=self.listen_addr,
        )

    async def _run_impl(self):
        async with trio.open_nursery() as nursery:
            nursery.start_soon(self._serve_tcp_forward)
            try:
                await trio.sleep_forever()  # 等待，直到被 Ctrl+C 中断
            except KeyboardInterrupt:
                logger.warning("收到 Ctrl+C, 正在优雅退出...")
                nursery.cancel_scope.cancel()  # 通知所有子任务取消

    def run(self):
        logger.info(
            "百度代理服务器启动! 监听地址:[{}:{}] 目标地址:[{}:{}]",
            self.listen_addr,
            self.listen_port,
            self.baidu_proxy_host,
            self.baidu_proxy_port,
        )
        trio.run(self._run_impl)
        pass
