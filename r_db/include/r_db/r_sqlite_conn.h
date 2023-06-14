
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

namespace r_db
{

r_utils::r_nullable<std::string> to_scalar(const std::vector<std::map<std::string, r_utils::r_nullable<std::string>>>& row);

class r_sqlite_conn final
{
public:
    R_API r_sqlite_conn(const std::string& fileName, bool rw = true);
    R_API r_sqlite_conn(const r_sqlite_conn&) = delete;
    R_API r_sqlite_conn(r_sqlite_conn&& obj) noexcept;

    R_API ~r_sqlite_conn() noexcept;

    R_API r_sqlite_conn& operator=(const r_sqlite_conn&) = delete;
    R_API r_sqlite_conn& operator=(r_sqlite_conn&&) noexcept;

    R_API std::vector<std::map<std::string, r_utils::r_nullable<std::string>>> exec(const std::string& query) const;

    R_API std::string last_insert_id() const;

private:
    void _clear() noexcept;

    sqlite3* _db;
    bool _rw;
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
