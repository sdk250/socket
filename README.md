# 成品脚本

[MLKit](https://github.com/sdk250/MLKit)

# 介绍

这是一个 **`专门`** 用于百度直连的http轻量代理

# Compile

```shell
make
```

If it is not working, please try:

```shell
clang -m64 -O3 -Wall -lc -o esocket thread_socket.c driver.c
```

# Usage

```shell
./esocket -h
Usage of ./esocket:
        -p      <PORT>
                Set PORT while running
        -l      Show running log
        -u      <UID>
                Set UID while running
        -r      <SERVER ADDRESS>
                Set IP of peer
        -d      Start daemon service
        -h      Show this message

# 基本使用方法
./esocket -p 8080 -l -r 14.215.182.75

curl -vx 'http://127.0.0.1:8080' 'https://4.ipw.cn/'
```

程序本体仅依赖标准C库，所以理论上可以在任意标准Linux环境中使用，Android推荐 [Termux](https://github.com/termux/termux-app)

`esocket` 的能够接受标准 `HTTP代理协议` ，以及来自 `TPROXY` 转发的流量。

在 Android 中快速使用 HTTP代理的方法可以通过自带的APN设置，但是跳点会很大。
