from lib import proxy
from loguru import logger

from lib.cli import run_cli


def main(
    listen_port: int,
    baidu_proxy_host: str,
    open_log: bool,
):
    if open_log:
        logger.add("./logs/proxy.log", rotation="1 day", enqueue=True)  # 开启异步日志
    # try:
    proxy.BaiduProxyServer(
        listen_port=listen_port,
        baidu_proxy_host=baidu_proxy_host,
        open_log=open_log,
    ).run()
    # except KeyboardInterrupt:
    #     logger.warning("即将退出程序...")
    pass


if __name__ == "__main__":
    run_cli(main)
