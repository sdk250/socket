# 百度代理池

## 介绍

用于利用百度代理池来做 `http`/`https` 代理

## 百度代理池差异

不管发送`http`还是`https`请求，都需要先发送一个`CONNECT`请求，并且此请求，不允许携带`Host`和`User-Agent`;可以使用以下命令验证

```sh
# 获取本机 公网IP
curl https://4.ipw.cn

# 使用百度代理池代理 https 请求, 获取经过代理之后的公网IP; 如果IP与上条命令结果不一致，则代表成功使用代理
curl -x cloudnproxy.baidu.com:443 https://4.ipw.cn \
  --proxy-header "Host:" \
  --proxy-header "User-Agent;"
```

## 流程

1. 先构造一个`CONNECT`请求，发送到代理池
2. 接收代理池的响应`HTTP/1.1 200 Connection established`.
3. 如果接收到的是`http`请求，则丢弃代理池的响应，把代理请求发送到代理池，然后把响应返回给客户端，完成代理。
4. 如果接收到 https 请求,则把代理池返回的响应转发给客户端，然后开始接收客户端的数据转发给代理池，接收代理池的响应转发给客户端。完成代理。

## 环境搭建 & 程序执行

```sh
# 如果是 ubuntu 也许需要先使用apt安装 venv 模块
sudo apt install python3.12-venv
# 创建 python 虚拟环境; python 或者 python3
python -m venv .venv
# 使用虚拟环境
source .venv/bin/activate
# 安装依赖库; -v 显示详细信息
python -m pip install -r requirements.txt -v
# 启动代理程序
python main.py

# 进入测试目录
cd tests

# 安装测试所需的依赖库; -v 显示详细信息
python -m pip install -r requirements.txt -v

# 测试本地代理是否生效, 如果生效则是 4个passed;
# 此测试测试了，ip变化,post发送512KB数据测试； 分别包含http/https两种版本的测试
# httpbin.py::test_proxy_changes_origin[http] PASSED     [ 25%]
# httpbin.py::test_proxy_changes_origin[https] PASSED    [ 50%]
# httpbin.py::test_proxy_handles_large_post[http] PASSED [ 75%]
# httpbin.py::test_proxy_handles_large_post[https] PASSED[100%]
pytest httpbin.py -v
```

## 程序帮助

```sh
# 移除了-u和-d选项; 因为无法简单的实现跨平台.
# -u选项建议使用sudo -u实现
# -d选项建议使用第三方程序实现; 比如 tmux, systemctl, screen, nohup 等等
python main.py --help

 Usage: main.py [OPTIONS]

╭─ Options ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────╮
│         -p      INTEGER  设置端口 [default: 8080]                                                                            │
│         -r      TEXT     设置百度代理IP [default: cloudnproxy.baidu.com]                                                     │
│         -l               显示请求日志                                                                                        │
│ --help                   Show this message and exit.                                                                         │
╰──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────╯
```
