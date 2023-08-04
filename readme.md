# 项目介绍

学习linux系统下的编程后，写的项目。使用C++完成一个响应HTTP的Web服务器。

## 功能介绍

+ 利用线程池和IO复用技术（epoll）实现模拟 Proactor 的高并发模型；
+ 利用有限状态机解析HTTP请求报文，支持 GET 请求和 POST请求；
+ 改写小根堆，实现了关闭超时的非活动连接的定时器；
+ 利用 RAII 机制实现数据库连接池，支持用户登录注册。

## 环境配置

创建对应 MySQL 库

```mysql
创建用户：
create user 'username'@'IP地址' IDENTIFIED by 'password';

创建库：
create database database_name;

创建表:
use database_name;
create table user(username char(50) NULL, password char(50) NULL);


注意数据库默认端口：3306
```

## 编译与启动

```bash
默认开放端口：10000
make
./server 10000
```

## 压力测试

```bash
./webbench-1.5/webbench -c 10000 -t 10 http://ip:port/index.html
```

