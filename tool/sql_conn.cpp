#include "sql_conn.h"

using namespace std;

SqlConnPool::SqlConnPool() {
    m_useCount = 0;
    m_freeCount = 0;
}

SqlConnPool::~SqlConnPool() {
    ClosePool();
}

SqlConnPool* SqlConnPool::Instance() {
    static SqlConnPool connPool;
    return &connPool;
}

void SqlConnPool::Init(const char* host, int port,
            const char* user,const char* pwd, const char* dbName,
            int connSize = 10) {
    if(connSize <= 0){
        return;
    }
    for (int i = 0; i < connSize; i++) {
        MYSQL *sql = nullptr;
        sql = mysql_init(sql);
        if (!sql) {
            // LOG_ERROR("MySql init error!");
            return;
        }
        sql = mysql_real_connect(sql, host, user, pwd, dbName, port, nullptr, 0);
        if (!sql) {
            // LOG_ERROR("MySql Connect error!");
        }
        m_connQue.push(sql);
    }
    MAX_CONN = connSize;
    m_sem = new sem(MAX_CONN);
}

MYSQL* SqlConnPool::GetConn() {
    MYSQL *sql = nullptr;
    if(m_connQue.empty()){
        // LOG_WARN("SqlConnPool busy!");
        return nullptr;
    }
    m_sem->wait();
    {
        m_mtx.lock();
        sql = m_connQue.front();
        m_connQue.pop();
        m_mtx.unlock();
    }
    return sql;
}

void SqlConnPool::FreeConn(MYSQL* sql) {
    if(sql == nullptr){
        return;
    }
    m_mtx.lock();
    m_connQue.push(sql);
    m_mtx.unlock();
    m_sem->post();
}

void SqlConnPool::ClosePool() {
    m_mtx.lock();
    while(!m_connQue.empty()) {
        MYSQL* item = m_connQue.front();
        m_connQue.pop();
        mysql_close(item);
    }
    mysql_library_end();        
}

int SqlConnPool::GetFreeConnCount() {
    m_mtx.lock();
    return m_connQue.size();
}
