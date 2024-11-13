# 成品脚本
[MLKit](https://github.com/sdk250/MLKit)
# 介绍
这是一个`专门`用于百度直连的http轻量代理<br>
使用多线程编写，本身编译出来极为轻量<br>
修改`用于连接的IP`在`thread_socket.h`中的宏定义`SERVER_ADDR`，将它修改为你想使用的百度直连IP，然后重新编译即可。<br>
亦或者运行时加上 `-r` 参数来指定<br>
# Compile
```shell
make
```
If it is not working, please try:
```shell
clang -m64 -O3 -Wall -lpthread -o thread_socket thread_socket.c driver.c
```
# Usage
```shell
./thread_socket -h
Usage of ./thread_socket:
        -p      <PORT>
                Set PORT while running
        -l      Show running log
        -u      <UID>
                Set UID while running
        -r      <SERVER ADDRESS>
                Set IP of peer
        -d      Start daemon service
        -h      Show this message
```
编译以后可以在任意终端上运行，比如安卓的Termux<br>
使用的最直接的使用方法就是运行后在手机的APN设置里面设置代理:<br>
127.0.0.1 端口 默认为8080
# Email
` hexfya@gmail.com `

