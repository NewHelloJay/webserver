#include <iostream>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <functional>
#include "./tool/threadpool.h"
#include "./http/http_conn.h"
#include "./tool/sql_connRALL.h"
#include "./tool/sql_conn.h"
#include "./heaptimer/heap_timer.h"

#define MAX_FD 65535
#define MAX_EVENT_NUMBER 10000
#define ACTIVE_TIME -1

void update_active_time(HeapTimer* timer, int connfd, int timeoutMS){
    if(connfd < 0){
        return;
    }
    timer->adjust(connfd, timeoutMS);
}

// 添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool one_shot);

// 删除epoll中的指定文件描述符
extern void removefd(int epollfd, int fd);

// 修改epoll中的指定文件描述符
extern void modfd(int epollfd, int fd, int ev);

int main(int argc, char* argv[]){
    
    if(argc <= 1){
        std::cout << "按照如下格式运行: " << argv[0] << " port_number" << std::endl;
        exit(-1);
    }

    // 获取端口号
    int port = atoi(argv[1]);

    // 初始化线程池
    threadpool<http_conn>* pool = nullptr;
    try{
        pool = new threadpool<http_conn>();
    } catch(...){
        exit(-1);
    }


    // 初始化 MySQL
    SqlConnPool::Instance()->Init("localhost", 0, "webserver", "jay", "myclient", 6);

    // 初始化timer
    int timeoutMS = ACTIVE_TIME;
    int timeMS = -1;
    HeapTimer* timer = new HeapTimer;

    // 创建一个数组保存所有的客户端信息
    http_conn* users = new http_conn[MAX_FD];

    // 创建套接字
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if(listenfd == -1){
        perror("socket");
        exit(-1);
    }

    //设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    int ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    if(ret == -1){
        perror("bind");
        exit(-1);
    }

    // 监听
    ret = listen(listenfd, 5);
    if(ret == -1){
        perror("listen");
        exit(-1);
    }

    // 创建epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while(true){
        if(timeoutMS > 0){
            timeMS = timer->GetNextTick();
        }
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, timeMS);
        if(num < 0 && errno != EINTR){
            std::cout << "epoll failure" << std::endl;
            break;
        }

        for(int i = 0; i < num; i++){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){
                // 有客户端连接进来
                // std::cout << "current users' numbers:" << users[0].m_user_count << std::endl;
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);
                if(connfd == -1){
                    perror("accept");
                    exit(-1);
                }
                if(http_conn::m_user_count >= MAX_FD){
                    // 目前连接数满了，发送客户端信息：服务器满
                    close(connfd);
                    continue;
                }
                users[connfd].init(connfd, client_address);
                if(timeoutMS > 0){
                    timer->add(connfd, timeoutMS, std::bind(&http_conn::colse_conn, &users[connfd]));
                }
            }
            else if(events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)){
                // 对方异常断开或者错误等事件
                users[sockfd].colse_conn();
            }
            else if(events[i].events & EPOLLIN){
                if(timeoutMS > 0){
                    update_active_time(timer, sockfd, timeoutMS);
                }
                if(users[sockfd].read()){
                    // 一次性把所有数据都读完
                    pool->append(users + sockfd);
                }else{
                    users[sockfd].colse_conn();
                }
            }
            else if(events[i].events & EPOLLOUT){
                if(timeoutMS > 0){
                    update_active_time(timer, sockfd, timeoutMS);
                }
                if(!users[sockfd].write()){
                    users[sockfd].colse_conn();
                }
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;

    return 0;
}