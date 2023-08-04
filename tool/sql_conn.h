#ifndef SQLCONN_H
#define SQLCONN_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <thread>
#include "locker.h"

class SqlConnPool {
public:
    static SqlConnPool *Instance();

    void Init(const char* host, int port,
              const char* user,const char* pwd, 
              const char* dbName, int connSize);

    MYSQL *GetConn();
    void FreeConn(MYSQL * conn);
    int GetFreeConnCount();

    void ClosePool();

private:
    SqlConnPool();
    ~SqlConnPool();

    int MAX_CONN;
    int m_useCount;
    int m_freeCount;

    std::queue<MYSQL *> m_connQue;
    locker m_mtx;
    sem* m_sem;
};


#endif