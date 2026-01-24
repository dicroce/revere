
#ifndef _r_db_r_sqlite_pager_h
#define _r_db_r_sqlite_pager_h

#include "r_db/r_sqlite_conn.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <string>
#include <map>
#include <vector>

namespace r_db
{

class r_sqlite_pager final
{
public:
    R_API r_sqlite_pager(const std::string& columnsToInclude, const std::string& tableName, const std::string& indexColumn, uint32_t requestedPageSize);
    R_API r_sqlite_pager(const r_sqlite_pager& obj) = default;
    R_API r_sqlite_pager(r_sqlite_pager&& obj) noexcept = default;

    R_API ~r_sqlite_pager() noexcept = default;

    R_API r_sqlite_pager& operator=(const r_sqlite_pager& obj) = default;
    R_API r_sqlite_pager& operator=(r_sqlite_pager&& obj) noexcept = default;

    R_API void find(const r_sqlite_conn& conn, const std::string& val = std::string());
    R_API std::map<std::string, r_utils::r_nullable<std::string>> current() const;
    R_API void next(const r_sqlite_conn& conn);
    R_API void prev(const r_sqlite_conn& conn);
    R_API bool valid() const;
    R_API void end(const r_sqlite_conn& conn);

private:
    std::string _columnsToInclude;
    std::string _tableName;
    std::string _indexColumn;
    uint32_t _requestedPageSize;
    size_t _index;
    std::vector<std::map<std::string, r_utils::r_nullable<std::string>>> _results;
};

}

#endif
