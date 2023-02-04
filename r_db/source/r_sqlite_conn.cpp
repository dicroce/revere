
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
    return row.front().begin()->second;
}

r_sqlite_conn::r_sqlite_conn(const string& fileName, bool rw) :
    _db(nullptr),
    _rw(rw)
{
    int numRetries = DEFAULT_NUM_OPEN_RETRIES;

    int ret = 0;

    while(numRetries > 0)
    {
        int flags = SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;

        flags |= (_rw)?SQLITE_OPEN_READWRITE:SQLITE_OPEN_READONLY;

        ret = sqlite3_open_v2(fileName.c_str(), &_db, flags, nullptr );

        if(ret == SQLITE_OK)
        {
            sqlite3_busy_timeout(_db, BUSY_TIMEOUT_MILLIS);
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
            else R_STHROW(r_internal_exception, ("Query (%s) to db failed. Cause:", query.c_str(), sqlite3_errmsg(_db)));
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

void r_sqlite_conn::_clear() noexcept
{
    if(_db)
    {
        if(sqlite3_close(_db) != SQLITE_OK)
            R_LOG_ERROR("Unable to close database. Leaking db handle. Cause: %s", sqlite3_errmsg(_db));

        _db = nullptr;
    }
}
