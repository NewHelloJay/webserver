#include "http_conn.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <errno.h>
#include <cstring>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/uio.h>

using namespace std;

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

const char* doc_root = "/home/jay/web_server/resources";

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible...\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have premssion to get file from this server\n";
const char* error_404_title = "Not Fount";
const char* error_404_form = "The requested file was not found on this server. \n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

void setnonblocking(int fd){
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

// 向epoll中添加文件描述符
void addfd(int epollfd, int fd, bool one_shot){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLHUP;

    if(one_shot){
        event.events = event.events | EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 向epoll中删除文件描述符
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改epoll中的指定文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

http_conn::http_conn(){

}

http_conn::~http_conn(){

}

// 由线程池中的工作线程调用，这是处理http请求的入口函数
void http_conn::process(){
    // std::cout << "parse request, create response" << std::endl;
     // 解析http请求
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    // std::cout << "start response..." << std::endl;

     // 生产响应
     bool write_ret = process_write(read_ret);
     if(!write_ret){
        colse_conn();
     }
     modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

void http_conn::init(int sockfd, struct sockaddr_in &client_address){
    
    m_sockfd = sockfd;
    m_address = client_address;

    // 端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addfd(m_epollfd, m_sockfd, true);
    m_user_count++;

    init();
}

void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_checked_index = 0;
    m_start_line = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_url = 0;
    m_method = GET;
    m_version = 0;
    m_content_type = 0;
    m_linger = false;
    m_content_length = 0;
    m_file_address = nullptr;
    bzero(m_read_buff, READ_BUFFER_SIZE);
    bzero(m_write_buff, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

void http_conn::colse_conn(){
    if(m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read(){
    if(m_read_idx >= READ_BUFFER_SIZE){
        return false;
    }
    int bytes_read = 0;
    while(true){
        bytes_read = recv(m_sockfd, m_read_buff + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                // 没有数据
                break;
            }
            return false;
        }
        else if(bytes_read == 0){
            // 对面关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }
    // std::cout << m_read_buff << std::endl;
    return true;
}

bool http_conn::write(){
    int temp = 0;
    int bytes_have_send = 0;  // 已经发送的字节
    int bytes_to_send = m_write_idx;  // 将要发送的字节，m_write_idx 写缓冲区中待发送的字节数

    if(bytes_to_send == 0){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }
    // std::cout << "response heads: " <<  m_write_buff << std::endl;
    // std::cout << "response content: " <<  m_file_address << std::endl;
    while(true){
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= -1){
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，
            if(errno == EAGAIN){
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if(bytes_to_send <= bytes_have_send){
            // 发送http响应成功，根据http中的Connection字段决定是否立刻断开连接
            unmap();
            if(m_linger){
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            }
            else{
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }

    return true;
}

int http_conn::ConverHex(char ch){
    if(ch >= 'A' && ch <= 'F'){
        return ch - 'A' + 10;
    }
    if(ch >= 'a' && ch <= 'f'){
        return ch - 'a' + 10;
    }
    return ch;
}

// 主状态机，解析请求
HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char* text = 0;

    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) 
        || (line_status = parse_line()) == LINE_OK){
        // 解析到了一行完整的数据，或者解析到了请求体（也是完整的数据）

        //获取一行数据
        text = get_line();
        m_start_line = m_checked_index;
        // std::cout << "got 1 http line: " << text << std::endl;

        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:{
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return ret;
                }
                break;
            }
            case CHECK_STATE_HEADER:{
                ret = parse_headers(text);
                if(ret == BAD_REQUEST){
                    return ret;
                }else if(ret == GET_REQUEST){
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:{
                ret = parse_content(text);
                if(ret == GET_REQUEST){
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:{
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

// 解析http请求行，获得请求方法，目标URL，http版本
HTTP_CODE http_conn::parse_request_line(char* text){
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");

    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';
    char* method = text;
    if(strcasecmp(method, "GET") == 0){
        m_method = GET;
    }
    else if(strcasecmp(method, "POST") == 0){
        m_method = POST;
    }
    else{
        return BAD_REQUEST;
    }

    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if(!m_version){
        return BAD_REQUEST;
    }

    // /index.html\0HTTP/1.1
    *m_version++ = '\0';
    if(strcasecmp(m_version, "HTTP/1.1") != 0){
        return BAD_REQUEST;
    }

    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }
    // 主状态机状态改变
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

HTTP_CODE http_conn::parse_headers(char* text){
    // 遇到空行，表示头部字段解析完毕
    if(text[0] == '\0'){
        // 如果http请求有消息体，则还需要读取m_content_length字节的消息体
        cout << "解析主体长度：" << m_content_length << endl;
        if(m_content_length != 0){
            // 主状态机状态改变
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0){
        // 处理Connection字段 Connection: keep-alive
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0){
            m_linger = true;
        }
    }
    else if(strncasecmp(text, "Content-Length:", 15) == 0){
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else if(strncasecmp(text, "Content-Type:", 13) == 0){
        text += 13;
        text += strspn(text, " \t");
        m_content_type = text;
    }
    else{
        // std::cout << "other request headers" << std::endl;
    }
    return NO_REQUEST;
}

HTTP_CODE http_conn::parse_content(char* text){
    cout << "开始解析body" << endl;
    m_body = text;
    parse_post();
    return GET_REQUEST;
}

void http_conn::parse_post(){
    cout << m_method << endl;
    cout << m_content_type << endl;
    if(m_method == POST && strcasecmp(m_content_type, "application/x-www-form-urlencoded") == 0){
        parse_from_urlencoded();
        cout << "解析post, 确认身份" << endl;
        if(strcasecmp(m_url, "/register") == 0 || strcasecmp(m_url, "/login") == 0){
            bool isLogin = (strcasecmp(m_url, "/login") == 0);
            cout << "准备解析post, 确认身份" << endl;
            if(user_verify(m_post["username"], m_post["password"], isLogin)){
                strcpy(m_url, "/welcome.html");
            }
            else{
                strcpy(m_url, "/error.html");
            }
        }
    }
}

void http_conn::parse_from_urlencoded(){
    if(m_body.size() == 0){
        return;
    }

    string key, value;
    int num = 0;
    int n = m_body.size();
    int i = 0, j = 0;

    for(; i < n; i++) {
        char ch = m_body[i];
        switch (ch) {
        case '=':
            key = m_body.substr(j, i - j);
            j = i + 1;
            break;
        case '+':
            m_body[i] = ' ';
            break;
        case '%':
            num = ConverHex(m_body[i + 1]) * 16 + ConverHex(m_body[i + 2]);
            m_body[i + 2] = num % 10 + '0';
            m_body[i + 1] = num / 10 + '0';
            i += 2;
            break;
        case '&':
            value = m_body.substr(j, i - j);
            j = i + 1;
            m_post[key] = value;
            // LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
            break;
        default:
            break;
        }
    }
    if(j > i){
        return;
    }
    if(m_post.count(key) == 0 && j < i) {
        value = m_body.substr(j, i - j);
        m_post[key] = value;
    }
}

bool http_conn::user_verify(const string &name, const string &pwd, bool isLogin){
    cout << "开始确认身份" << endl;
    cout << "name: " << name << "   password: " <<  pwd << endl;
    if(name == "" || pwd == "") { 
        return false;
    }
    // LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());
    MYSQL* sql;
    sqlConnRAII(&sql,  SqlConnPool::Instance());
    if(sql == nullptr){
        cout << "数据库返回失败" << endl;
        return false;
    }
    cout << "数据库打开成功" << endl;

    bool flag = false;
    unsigned int j = 0;
    char order[256] = { 0 };
    MYSQL_FIELD *fields = nullptr;
    MYSQL_RES *res = nullptr;
    
    if(!isLogin) {
        flag = true;
    }
    /* 查询用户及密码 */
    snprintf(order, 256, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    // LOG_DEBUG("%s", order);

    if(mysql_query(sql, order)) { 
        mysql_free_result(res);
        return false; 
    }
    
    res = mysql_store_result(sql);
    j = mysql_num_fields(res);
    fields = mysql_fetch_fields(res);
    cout << "数据库查询成功，返回查询结果：" << j << "条" << endl;

    while(MYSQL_ROW row = mysql_fetch_row(res)) {
        // LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        cout << "MYSQL ROW: " << row[0] << "  " << row[1] << endl;
        string password(row[1]);
        /* 登录行为 且 用户名能查询到*/
        if(isLogin) {
            if(pwd == password) { 
                flag = true;
                cout << "login success" << endl;
            }
            else {
                flag = false;
                // LOG_DEBUG("pwd error!");
                cout << "password error" << endl;
            }
        } 
        else { 
            flag = false; 
            // LOG_DEBUG("user used!");
        }
    }
    mysql_free_result(res);

    /* 注册行为 且 用户名未被使用*/
    if(!isLogin && flag == true) {
        // LOG_DEBUG("regirster!");
        bzero(order, 256);
        snprintf(order, 256,"INSERT INTO user(username, password) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        // LOG_DEBUG( "%s", order);
        if(mysql_query(sql, order)) { 
            // LOG_DEBUG( "Insert error!");
            flag = false; 
            cout << "register fail" << endl;
        }
        flag = true;
    }
    SqlConnPool::Instance()->FreeConn(sql);
    // LOG_DEBUG( "UserVerify success!!");
    return flag;
}

// 解析一行，判断依据\r\n
LINE_STATUS http_conn::parse_line(){
    char temp;
    for(; m_checked_index < m_read_idx; m_checked_index++){
        temp = m_read_buff[m_checked_index];
        if(temp == '\r'){
            if((m_checked_index + 1) == m_read_idx){
                return LINE_OPEN;
            }
            else if(m_read_buff[m_checked_index + 1] == '\n'){
                m_read_buff[m_checked_index++] = '\0';
                m_read_buff[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n'){
            if(m_checked_index > 1 && m_read_buff[m_checked_index - 1] == '\r'){
                m_read_buff[m_checked_index - 1] = '\0';
                m_read_buff[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // 获取m_real_file文件相关的状态信息，-1失败，0成功
    if(stat(m_real_file, &m_file_stat) < 0){
        return NO_RESOURCE;
    }

    // 判断访问权限
    if(!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    // 只读方式打开
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::process_write(HTTP_CODE read_ret){
    switch(read_ret){
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)){
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)){
                return false;
            }
            break;
        case NO_REQUEST:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)){
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)){
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buff;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buff;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

bool http_conn::add_response(const char* format, ...){
    if(m_write_idx >= WRITE_BUFFER_SIZE){
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buff + m_write_idx, WRITE_BUFFER_SIZE - 1- m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)){
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len){
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
    return true;
}

bool http_conn::add_content_type(){
    return add_response("Content-Type: %s\r\n", "text/html");
}

bool http_conn::add_content_length(int content_len){
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger(){
    return add_response("Connection: %s\r\n", m_linger == true ? "keep-alive" : "close");
}

bool http_conn::add_blank_line(){
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content){
    return true;
}

