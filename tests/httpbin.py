import socket
import pytest
import requests
import os
import base64

# 代理配置（根据实际情况修改地址和端口）
PROXY_ADDRESS = "http://localhost:8080"  # 代理服务器地址
# ubuntu24_proxy_ip = socket.gethostbyname("xb1520-ubuntu24.local")
# PROXY_ADDRESS = f"http://{ubuntu24_proxy_ip}:8080"  # 代理服务器地址


@pytest.fixture
def proxy_enabled():
    """启用代理的配置"""
    return {
        "http": PROXY_ADDRESS,
        "https": PROXY_ADDRESS,
    }


@pytest.fixture
def proxy_disabled():
    """禁用代理的配置"""
    return {
        "http": None,
        "https": None,
    }


@pytest.fixture
def random_data():
    # 生成512KB随机二进制数据
    return os.urandom(1024 * 512)


@pytest.mark.parametrize("protocol", ["http", "https"])
def test_proxy_changes_origin(proxy_enabled, proxy_disabled, protocol):
    """验证使用代理后 origin 发生变化"""
    # 获取无代理时的真实IP
    resp_no_proxy = requests.get(f"{protocol}://httpbin.org/ip", proxies=proxy_disabled)
    origin_real = resp_no_proxy.json()["origin"]

    # 获取代理后的IP
    resp_with_proxy = requests.get(
        f"{protocol}://httpbin.org/ip", proxies=proxy_enabled
    )
    origin_proxied = resp_with_proxy.json()["origin"]

    # 重要断言：代理前后的IP不同
    assert (
        origin_real != origin_proxied
    ), f"代理未生效! 真实IP: {origin_real}, 代理后IP: {origin_proxied}"


@pytest.mark.parametrize("protocol", ["http", "https"])
def test_proxy_handles_large_post(proxy_enabled, random_data, protocol):
    """验证代理正确处理大体积POST数据"""

    # 通过代理发送POST请求
    resp = requests.post(
        f"{protocol}://httpbin.org/post",
        data=random_data,
        proxies=proxy_enabled,
        headers={"Content-Type": "application/octet-stream"},
    )

    # 验证响应状态码
    assert resp.status_code == 200, f"请求失败，状态码: {resp.status_code}"
    returned_data = base64.b64decode(
        resp.json()["data"].split(",")[1]
    )  # 解密返回的数据
    assert returned_data == random_data, "返回数据与发送数据不一致！"
