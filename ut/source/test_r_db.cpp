
#include "test_r_db.h"
#include "r_db/r_sqlite_conn.h"
#include "r_db/r_sqlite_pager.h"
#include "r_utils/r_file.h"

#include <chrono>
#include <thread>
#include <climits>
#include <numeric>
#include <algorithm>

using namespace std;
using namespace std::chrono;
using namespace r_utils;
using namespace r_db;

REGISTER_TEST_FIXTURE(test_r_db);

void test_r_db::setup()
{
    r_sqlite_conn conn("test.db", true);
}

void test_r_db::teardown()
{
    r_fs::remove_file("test.db");
}

void test_r_db::test_db_ctor()
{
    RTF_ASSERT(r_fs::file_exists("test.db"));
}

void test_r_db::test_db_insert()
{
    r_sqlite_conn conn("test.db", true);
    conn.exec("CREATE TABLE worker_bees(sesa_id TEXT, name TEST, id INTEGER PRIMARY KEY AUTOINCREMENT);");
    conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('120129', 'Tony Di Croce');");
    auto pk = conn.last_insert_id();
    RTF_ASSERT(pk == "1");
    auto results = conn.exec("SELECT * FROM worker_bees;");
    RTF_ASSERT(results.size() == 1);
    RTF_ASSERT(results[0]["sesa_id"] == "120129");
    RTF_ASSERT(results[0]["name"] == "Tony Di Croce");
    RTF_ASSERT(results[0]["id"] == "1");
}

void test_r_db::test_db_paging()
{
    r_sqlite_conn conn("test.db", true);

    r_sqlite_transaction(conn, [](const r_sqlite_conn& conn){
        conn.exec("CREATE TABLE worker_bees(sesa_id TEXT, name TEST, id INTEGER PRIMARY KEY AUTOINCREMENT);");
        conn.exec("CREATE INDEX worker_bees_sesa_id_idx ON worker_bees(sesa_id);");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123456', 'Alan Turing');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123458', 'John Von Neumann');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123460', 'Alonzo Church');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123462', 'Donald Knuth');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123464', 'Dennis Ritchie');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123466', 'Claude Shannon');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123468', 'Bjarne Stroustrup');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123470', 'Linus Torvalds');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123472', 'Larry Wall');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123474', 'Yann LeCun');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123476', 'Yoshua Bengio');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123478', 'Guido van Rossum');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123480', 'Edsger W. Dijkstra');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123482', 'Ken Thompson');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123484', 'Grace Hopper');");
        conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('123486', 'Ada Lovelace');");
    });

    // First, do some basic paging..
    auto result = conn.exec("SELECT * FROM worker_bees LIMIT 4 OFFSET 0;");
    RTF_ASSERT(result.size() == 4);
    RTF_ASSERT(result[0]["name"] == "Alan Turing");
    RTF_ASSERT(result[1]["name"] == "John Von Neumann");
    RTF_ASSERT(result[2]["name"] == "Alonzo Church");
    RTF_ASSERT(result[3]["name"] == "Donald Knuth");

    result = conn.exec("SELECT * FROM worker_bees LIMIT 4 OFFSET 4;");
    RTF_ASSERT(result.size() == 4);
    RTF_ASSERT(result[0]["name"] == "Dennis Ritchie");
    RTF_ASSERT(result[1]["name"] == "Claude Shannon");
    RTF_ASSERT(result[2]["name"] == "Bjarne Stroustrup");
    RTF_ASSERT(result[3]["name"] == "Linus Torvalds");

    // This is similar to a find on a key
    result = conn.exec("SELECT * from worker_bees WHERE sesa_id >= 123467 LIMIT 4;");
    RTF_ASSERT(result.size() == 4);
    RTF_ASSERT(result[0]["name"] == "Bjarne Stroustrup");
    RTF_ASSERT(result[1]["name"] == "Linus Torvalds");
    RTF_ASSERT(result[2]["name"] == "Larry Wall");
    RTF_ASSERT(result[3]["name"] == "Yann LeCun");

    // Next
    result = conn.exec(r_string_utils::format("SELECT * from worker_bees WHERE sesa_id > %s LIMIT 4;", result.back()["sesa_id"].value().c_str()));
    RTF_ASSERT(result.size() == 4);
    RTF_ASSERT(result[0]["name"] == "Yoshua Bengio");
    RTF_ASSERT(result[1]["name"] == "Guido van Rossum");
    RTF_ASSERT(result[2]["name"] == "Edsger W. Dijkstra");
    RTF_ASSERT(result[3]["name"] == "Ken Thompson");

    // Prev
    result = conn.exec(r_string_utils::format("SELECT * from worker_bees WHERE sesa_id < %s ORDER BY sesa_id DESC LIMIT 4;", result.front()["sesa_id"].value().c_str()));
    reverse(begin(result), end(result));
    RTF_ASSERT(result.size() == 4);
    RTF_ASSERT(result[0]["name"] == "Bjarne Stroustrup");
    RTF_ASSERT(result[1]["name"] == "Linus Torvalds");
    RTF_ASSERT(result[2]["name"] == "Larry Wall");
    RTF_ASSERT(result[3]["name"] == "Yann LeCun");
}
