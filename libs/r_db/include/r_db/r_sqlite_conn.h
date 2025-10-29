
#ifndef _r_db_r_sqlite_conn_h
#define _r_db_r_sqlite_conn_h

#include "sqlite3/sqlite3.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_macro.h"
#include <string>
#include <vector>
#include <map>

extern "C"
{
R_API int sqlite3_embedded_vfs_init(void);
}

namespace r_db
{

R_API r_utils::r_nullable<std::string> to_scalar(const std::vector<std::map<std::string, r_utils::r_nullable<std::string>>>& row);

class r_sqlite_stmt;

class r_sqlite_conn final
{
public:
    R_API r_sqlite_conn(const std::string& fileName, bool rw = true, bool wal = true);
    R_API r_sqlite_conn(const r_sqlite_conn&) = delete;
    R_API r_sqlite_conn(r_sqlite_conn&& obj) noexcept;

    R_API ~r_sqlite_conn() noexcept;

    R_API r_sqlite_conn& operator=(const r_sqlite_conn&) = delete;
    R_API r_sqlite_conn& operator=(r_sqlite_conn&&) noexcept;

    R_API std::vector<std::map<std::string, r_utils::r_nullable<std::string>>> exec(const std::string& query) const;

    R_API std::string last_insert_id() const;

    R_API r_sqlite_stmt prepare(const std::string& query) const;

    friend class r_sqlite_stmt;

private:
    void _clear() noexcept;

    sqlite3* _db;
    bool _rw;
};

class r_sqlite_stmt final
{
public:
    R_API r_sqlite_stmt(sqlite3* db, const std::string& query);
    R_API r_sqlite_stmt(const r_sqlite_stmt&) = delete;
    R_API r_sqlite_stmt(r_sqlite_stmt&& obj) noexcept;
    R_API ~r_sqlite_stmt() noexcept;
    
    R_API r_sqlite_stmt& operator=(const r_sqlite_stmt&) = delete;
    R_API r_sqlite_stmt& operator=(r_sqlite_stmt&&) noexcept;
    
    // Bind methods for different types
    R_API r_sqlite_stmt& bind(int index, int value);
    R_API r_sqlite_stmt& bind(int index, int64_t value);
    R_API r_sqlite_stmt& bind(int index, uint64_t value);
    R_API r_sqlite_stmt& bind(int index, double value);
    R_API r_sqlite_stmt& bind(int index, const std::string& value);
    R_API r_sqlite_stmt& bind(int index, const char* value);
    R_API r_sqlite_stmt& bind_null(int index);
    
    // Execute and get results
    R_API std::vector<std::map<std::string, r_utils::r_nullable<std::string>>> exec();
    
    // Execute without expecting results (INSERT, UPDATE, DELETE)
    R_API void exec_no_result();
    
    // Reset for reuse with different parameters
    R_API void reset();
    
private:
    void _clear() noexcept;
    
    sqlite3_stmt* _stmt;
    sqlite3* _db;  // Keep reference for error messages
};

template<typename T>
void r_sqlite_transaction(const r_sqlite_conn& db, T t)
{
    db.exec("BEGIN");
    try
    {
        t(db);
        db.exec("COMMIT");
    }
    catch(const r_utils::r_exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
        db.exec("ROLLBACK");
        throw ex;
    }
    catch(...)
    {
        printf("TRANS ROLLBACK!\n");
        fflush(stdout);
        R_LOG_ERROR("TRANS ROLLBACK!\n");
        db.exec("ROLLBACK");
        throw;
    }
}

}

#endif
