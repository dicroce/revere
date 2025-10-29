
#include "r_db/r_sqlite_conn.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include <thread>

using namespace r_db;
using namespace r_utils;
using namespace std;

static const int DEFAULT_NUM_OPEN_RETRIES = 5;
static const int BASE_SLEEP_MICROS = 500000;
static const int BUSY_TIMEOUT_MILLIS = 2000;

r_nullable<string> r_db::to_scalar(const vector<map<string, r_utils::r_nullable<string>>>& row)
{
    r_nullable<string> ret;
    if(!row.empty() && !row.front().begin()->second.is_null())
        ret.set_value(row.front().begin()->second.value());
    return ret;
}

r_sqlite_conn::r_sqlite_conn(const string& fileName, bool rw, bool wal) :
    _db(nullptr),
    _rw(rw)
{
    int numRetries = DEFAULT_NUM_OPEN_RETRIES;
    int ret = 0;
    while(numRetries > 0)
    {
        int flags = SQLITE_OPEN_NOMUTEX;
        if (_rw) {
            flags |= SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;  // Only add CREATE for R/W
        } else {
            flags |= SQLITE_OPEN_READONLY;  // No CREATE for read-only
        }
        
        //ret = sqlite3_open_v2(fileName.c_str(), &_db, flags, (embeddedvfs)?"embedded":nullptr);
        ret = sqlite3_open_v2(fileName.c_str(), &_db, flags, nullptr);
        if(ret == SQLITE_OK)
        {
            sqlite3_busy_timeout(_db, BUSY_TIMEOUT_MILLIS);

            if(wal)
                exec("PRAGMA journal_mode=WAL;");

            return;
        }
        if(_db != nullptr)
            _clear();
        std::this_thread::sleep_for(std::chrono::microseconds(((DEFAULT_NUM_OPEN_RETRIES-numRetries)+1) * BASE_SLEEP_MICROS));
        --numRetries;
    }
    R_STHROW(r_not_found_exception, ( "Unable to open database connection: %s, rw=%s, ret=%d", fileName.c_str(), (rw)?"true":"false", ret ));
}

r_sqlite_conn::r_sqlite_conn(r_sqlite_conn&& obj) noexcept :
    _db(std::move(obj._db)),
    _rw(std::move(obj._rw))
{
    obj._db = nullptr;
    obj._rw = false;
}

r_sqlite_conn::~r_sqlite_conn() noexcept
{
    _clear();
}

r_sqlite_conn& r_sqlite_conn::operator=(r_sqlite_conn&& obj) noexcept
{
    _clear();

    _db = std::move(obj._db);
    obj._db = nullptr;

    _rw = std::move(obj._rw);
    obj._rw = false;

    return *this;
}

vector<map<string, r_nullable<string>>> r_sqlite_conn::exec(const string& query) const
{
    if(!_db)
        R_STHROW(r_internal_exception, ("Cannot exec() on moved out instance of r_sqlite_conn."));

    vector<map<string, r_nullable<string>>> results;

    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v3(_db, query.c_str(), (int)query.length(), 0, &stmt, nullptr);
    if(rc != SQLITE_OK)
        R_STHROW(r_internal_exception, ("sqlite3_prepare_v2(%s) failed with: %s", query.c_str(), sqlite3_errmsg(_db)));
    if(stmt == NULL)
        R_STHROW(r_internal_exception, ("sqlite3_prepare_v2() succeeded but returned NULL statement."));

    try
    {
        bool done = false;
        while(!done)
        {
            rc = sqlite3_step(stmt);

            if(rc == SQLITE_DONE)
                done = true;
            else if(rc == SQLITE_ROW)
            {
                int columnCount = sqlite3_column_count(stmt);

                map<string, r_nullable<string>> row;

                for(int i = 0; i < columnCount; ++i)
                {
                    r_nullable<string> val;

                    switch(sqlite3_column_type(stmt, i))
                    {
                        case SQLITE_INTEGER:
                            val = r_string_utils::int64_to_s(sqlite3_column_int64(stmt, i));
                        break;
                        case SQLITE_FLOAT:
                            val = r_string_utils::double_to_s(sqlite3_column_double(stmt, i));
                        break;
                        case SQLITE_NULL:
                        break;
                        case SQLITE_TEXT:
                        default:
                        {
                            const char* tp = (const char*)sqlite3_column_text(stmt, i);
                            if(tp && (*tp != '\0'))
                                val = string(tp);
                        }
                        break;
                    }

                    row[sqlite3_column_name(stmt, i)] = val;
                }

                results.push_back(row);
            }
            else
            {
                R_STHROW(r_internal_exception, ("Query (%s) to db failed. Cause:", query.c_str(), sqlite3_errmsg(_db)));
            }
        }

        sqlite3_finalize(stmt);
    }
    catch(...)
    {
        sqlite3_finalize(stmt);
        throw;
    }

    return results;
}

string r_sqlite_conn::last_insert_id() const
{
    if(!_db)
        R_STHROW(r_internal_exception, ("Cannot last_insert_id() on moved out instance of r_sqlite_conn."));

    return r_string_utils::int64_to_s(sqlite3_last_insert_rowid(_db));
}

r_sqlite_stmt r_sqlite_conn::prepare(const string& query) const
{
    if(!_db)
        R_STHROW(r_internal_exception, ("Cannot prepare() on moved out instance of r_sqlite_conn."));
    
    return r_sqlite_stmt(_db, query);
}

void r_sqlite_conn::_clear() noexcept
{
    if(_db)
    {
        if(sqlite3_close(_db) != SQLITE_OK)
            R_LOG_ERROR("Unable to close database. Leaking db handle. Cause: %s", sqlite3_errmsg(_db));

        _db = nullptr;
    }
}

r_sqlite_stmt::r_sqlite_stmt(sqlite3* db, const string& query) :
    _stmt(nullptr),
    _db(db)
{
    int rc = sqlite3_prepare_v2(_db, query.c_str(), (int)query.length(), &_stmt, nullptr);
    if(rc != SQLITE_OK)
        R_STHROW(r_internal_exception, ("sqlite3_prepare_v2(%s) failed with: %s", query.c_str(), sqlite3_errmsg(_db)));
    if(_stmt == nullptr)
        R_STHROW(r_internal_exception, ("sqlite3_prepare_v2() succeeded but returned NULL statement."));
}

r_sqlite_stmt::r_sqlite_stmt(r_sqlite_stmt&& obj) noexcept :
    _stmt(std::move(obj._stmt)),
    _db(std::move(obj._db))
{
    obj._stmt = nullptr;
    obj._db = nullptr;
}

r_sqlite_stmt::~r_sqlite_stmt() noexcept
{
    _clear();
}

r_sqlite_stmt& r_sqlite_stmt::operator=(r_sqlite_stmt&& obj) noexcept
{
    _clear();
    
    _stmt = std::move(obj._stmt);
    obj._stmt = nullptr;
    
    _db = std::move(obj._db);
    obj._db = nullptr;
    
    return *this;
}

r_sqlite_stmt& r_sqlite_stmt::bind(int index, int value)
{
    if(!_stmt)
        R_STHROW(r_internal_exception, ("Cannot bind() on moved out instance of r_sqlite_stmt."));
    
    int rc = sqlite3_bind_int(_stmt, index, value);
    if(rc != SQLITE_OK)
        R_STHROW(r_internal_exception, ("sqlite3_bind_int() failed with: %s", sqlite3_errmsg(_db)));
    
    return *this;
}

r_sqlite_stmt& r_sqlite_stmt::bind(int index, int64_t value)
{
    if(!_stmt)
        R_STHROW(r_internal_exception, ("Cannot bind() on moved out instance of r_sqlite_stmt."));
    
    int rc = sqlite3_bind_int64(_stmt, index, value);
    if(rc != SQLITE_OK)
        R_STHROW(r_internal_exception, ("sqlite3_bind_int64() failed with: %s", sqlite3_errmsg(_db)));
    
    return *this;
}

r_sqlite_stmt& r_sqlite_stmt::bind(int index, uint64_t value)
{
    // Cast to int64_t since SQLite doesn't have unsigned 64-bit
    return bind(index, static_cast<int64_t>(value));
}

r_sqlite_stmt& r_sqlite_stmt::bind(int index, double value)
{
    if(!_stmt)
        R_STHROW(r_internal_exception, ("Cannot bind() on moved out instance of r_sqlite_stmt."));
    
    int rc = sqlite3_bind_double(_stmt, index, value);
    if(rc != SQLITE_OK)
        R_STHROW(r_internal_exception, ("sqlite3_bind_double() failed with: %s", sqlite3_errmsg(_db)));
    
    return *this;
}

r_sqlite_stmt& r_sqlite_stmt::bind(int index, const string& value)
{
    if(!_stmt)
        R_STHROW(r_internal_exception, ("Cannot bind() on moved out instance of r_sqlite_stmt."));
    
    // SQLITE_TRANSIENT makes SQLite copy the string
    int rc = sqlite3_bind_text(_stmt, index, value.c_str(), (int)value.length(), SQLITE_TRANSIENT);
    if(rc != SQLITE_OK)
        R_STHROW(r_internal_exception, ("sqlite3_bind_text() failed with: %s", sqlite3_errmsg(_db)));
    
    return *this;
}

r_sqlite_stmt& r_sqlite_stmt::bind(int index, const char* value)
{
    if(!_stmt)
        R_STHROW(r_internal_exception, ("Cannot bind() on moved out instance of r_sqlite_stmt."));
    
    if(value == nullptr)
        return bind_null(index);
    
    int rc = sqlite3_bind_text(_stmt, index, value, -1, SQLITE_TRANSIENT);
    if(rc != SQLITE_OK)
        R_STHROW(r_internal_exception, ("sqlite3_bind_text() failed with: %s", sqlite3_errmsg(_db)));
    
    return *this;
}

r_sqlite_stmt& r_sqlite_stmt::bind_null(int index)
{
    if(!_stmt)
        R_STHROW(r_internal_exception, ("Cannot bind_null() on moved out instance of r_sqlite_stmt."));
    
    int rc = sqlite3_bind_null(_stmt, index);
    if(rc != SQLITE_OK)
        R_STHROW(r_internal_exception, ("sqlite3_bind_null() failed with: %s", sqlite3_errmsg(_db)));
    
    return *this;
}

vector<map<string, r_nullable<string>>> r_sqlite_stmt::exec()
{
    if(!_stmt)
        R_STHROW(r_internal_exception, ("Cannot exec() on moved out instance of r_sqlite_stmt."));
    
    vector<map<string, r_nullable<string>>> results;
    
    bool done = false;
    while(!done)
    {
        int rc = sqlite3_step(_stmt);
        
        if(rc == SQLITE_DONE)
            done = true;
        else if(rc == SQLITE_ROW)
        {
            int columnCount = sqlite3_column_count(_stmt);
            
            map<string, r_nullable<string>> row;
            
            for(int i = 0; i < columnCount; ++i)
            {
                r_nullable<string> val;
                
                switch(sqlite3_column_type(_stmt, i))
                {
                    case SQLITE_INTEGER:
                        val = r_string_utils::int64_to_s(sqlite3_column_int64(_stmt, i));
                        break;
                    case SQLITE_FLOAT:
                        val = r_string_utils::double_to_s(sqlite3_column_double(_stmt, i));
                        break;
                    case SQLITE_NULL:
                        break;
                    case SQLITE_TEXT:
                    default:
                    {
                        const char* tp = (const char*)sqlite3_column_text(_stmt, i);
                        if(tp && (*tp != '\0'))
                            val = string(tp);
                    }
                    break;
                }
                
                row[sqlite3_column_name(_stmt, i)] = val;
            }
            
            results.push_back(row);
        }
        else
        {
            R_STHROW(r_internal_exception, ("Statement execution failed: %s", sqlite3_errmsg(_db)));
        }
    }
    
    return results;
}

void r_sqlite_stmt::exec_no_result()
{
    if(!_stmt)
        R_STHROW(r_internal_exception, ("Cannot exec_no_result() on moved out instance of r_sqlite_stmt."));
    
    int rc = sqlite3_step(_stmt);
    if(rc != SQLITE_DONE)
        R_STHROW(r_internal_exception, ("Statement execution failed: %s", sqlite3_errmsg(_db)));
}

void r_sqlite_stmt::reset()
{
    if(!_stmt)
        R_STHROW(r_internal_exception, ("Cannot reset() on moved out instance of r_sqlite_stmt."));
    
    sqlite3_reset(_stmt);
    sqlite3_clear_bindings(_stmt);
}

void r_sqlite_stmt::_clear() noexcept
{
    if(_stmt)
    {
        sqlite3_finalize(_stmt);
        _stmt = nullptr;
    }
    _db = nullptr;
}
