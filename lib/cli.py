# cli.py
import click
import typer
from typing import Callable

app = typer.Typer(add_completion=False)


def run_cli(callback: Callable):
    """
    封装 CLI 入口函数, callback 是主程序的处理函数
    """

    @app.command()
    def main(
        listen_port: int = typer.Option(8080, "-p", help="设置端口"),
        baidu_proxy_host: str = typer.Option(
            "cloudnproxy.baidu.com", "-r", help="设置百度代理IP"
        ),
        open_log: bool = typer.Option(False, "-l", help="显示请求日志"),
    ):

        # 将解析后的参数传给主程序定义的回调函数
        callback(
            listen_port=listen_port,
            baidu_proxy_host=baidu_proxy_host,
            open_log=open_log,
        )

    app()
