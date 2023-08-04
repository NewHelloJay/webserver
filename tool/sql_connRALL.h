#ifndef SQLCONNRALL_H
#define SQLCONNRALL_H

#include "sql_conn.h"

class sqlConnRAII{
public:
    sqlConnRAII(MYSQL** sql, SqlConnPool *connpool) {
        if(connpool == nullptr){
            return;
        }
        *sql = connpool->GetConn();
        m_sql = *sql;
        m_connpool = connpool;
    }

     ~sqlConnRAII() {
        if(m_sql) { m_connpool->FreeConn(m_sql); }
    }

private:
    MYSQL* m_sql;
    SqlConnPool* m_connpool;
};

#endif