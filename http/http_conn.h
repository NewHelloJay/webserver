#ifndef THHP_CONN_H
#define THHP_CONN_H

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <unordered_map>
#include "../tool/sql_connRALL.h"
#include "../tool/sql_conn.h"

// http请求方法，但我们只支持GET
enum METHOD {
    GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT
};

// 解析客户端请求时，主状态机的状态
enum CHECK_STATE{
    CHECK_STATE_REQUESTLINE = 0, // 当前正在分析请求行
    CHECK_STATE_HEADER,     // 当前正在分析头部字段
    CHECK_STATE_CONTENT     // 当前正在解析请求体
};

// 从状态机的三种可能状态，即行的读取状态
enum LINE_STATUS{
    LINE_OK = 0,  // 读取到一个完整的行
    LINE_BAD,   // 行出错
    LINE_OPEN   // 行数据尚且不完整
};

// 服务器处理HTTP请求可能的结果，报文解析的结果
enum HTTP_CODE{
    NO_REQUEST,  // 请求不完整，需要继续读取客户数据
    GET_REQUEST,  // 表示获得了一个完整的客户请求
    BAD_REQUEST,  // 表示客户请求语法错误
    NO_RESOURCE,   // 表示服务器没有资源
    FORBIDDEN_REQUEST,  // 表示客户对资源没有足够的访问权限
    FILE_REQUEST,   // 文件请求，获取文件成功
    INTERNAL_ERROR,  // 表示服务器内部错误
    CLOSED_CONNECTION  // 表示客户端以及关闭连接了
};

enum FILE_TYPE{
    HTML,
    JSON,
    CSS
};

class http_conn{
public:
    http_conn();
    ~http_conn();

    void process(); // 处理客户端请求
    void init(int sockfd, struct sockaddr_in &client_address); // 初始化连接
    void colse_conn(); //断开连接
    bool read(); // 非阻塞读
    bool write(); // 非阻塞写

    HTTP_CODE process_read();  // 解析http请求
    HTTP_CODE parse_request_line(char* text); // 解析请求行
    HTTP_CODE parse_headers(char* text);  // 解析请求头
    HTTP_CODE parse_content(char* text);  // 解析请求内容
    bool process_write(HTTP_CODE read_ret);
    bool add_response(const char* format, ...);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_len);
    bool add_content_length(int content_len);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char* content);

    LINE_STATUS parse_line();
    HTTP_CODE do_request();

    static int ConverHex(char ch);
    void parse_post();
    void parse_from_urlencoded();
    bool user_verify(const std::string &name, const std::string &pwd, bool isLogin);

private:
    void init();
    char* get_line(){
        return m_read_buff + m_start_line;
    }
    void unmap();
    FILE_TYPE ret_file_type(std::string str);

public:
    static int m_epollfd; // 所有的socket被注册到同一个epoll上面
    static int m_user_count; // 统计用户的数量
    static const int READ_BUFFER_SIZE = 2048; // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区大小
    static const int FILENAME_LEN = 200;

private:
    int m_sockfd; // 该连接的socket
    struct sockaddr_in m_address; // 通信的地址
    char m_read_buff[READ_BUFFER_SIZE]; // 读缓冲区
    int m_read_idx; // 表示读缓冲区中以及读入的客户端数据的最后一个字节的下一个位置
    char m_write_buff[WRITE_BUFFER_SIZE];
    int m_write_idx;

    int m_checked_index; // 当前正在分析的字符在读缓冲区的位置
    int m_start_line;    // 当前正在解析行的起始位置
    char* m_url;         // 请求目标文件的文件名
    char* m_version;     // 协议版本
    METHOD m_method;     // 请求方法
    char* m_host;        // 主机名
    char* m_content_type;
    bool m_linger;       // http请求是否保持连接
    long m_content_length; // 请求体的长度
    char m_real_file[FILENAME_LEN]; // 请求体完整路径
    struct stat m_file_stat;
    char* m_file_address;
    struct iovec m_iv[2];
    int m_iv_count;
    std::string m_body;
    std::unordered_map<std::string, std::string> m_post;
    FILE_TYPE m_file_type;

    CHECK_STATE m_check_state; // 主状态机当前所处的状态
};

#endif