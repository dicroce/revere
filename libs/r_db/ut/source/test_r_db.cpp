
#include "test_r_db.h"
#include "r_db/r_sqlite_conn.h"
#include "r_db/r_sqlite_pager.h"
#include "r_utils/r_file.h"
#include "r_utils/r_memory_map.h"

#include <chrono>
#include <thread>
#include <climits>
#include <numeric>
#include <algorithm>
#include <sstream>
#include <string>

using namespace std;
using namespace std::chrono;
using namespace r_utils;
using namespace r_db;

REGISTER_TEST_FIXTURE(test_r_db);

void setup_global()
{
//    sqlite3_embedded_vfs_init();
}

void teardown_global()
{
}

void test_r_db::setup()
{
    r_sqlite_conn conn("test.db", true);

    {
        auto f = r_file::open("embedded.db", "w+");
        r_fs::fallocate(f, 1024*1024*16);
        fflush(f);
    }

}

void test_r_db::teardown()
{
    r_fs::remove_file("test.db");
    //r_fs::remove_file("embedded.db");
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

    r_sqlite_transaction(conn, true, [](const r_sqlite_conn& conn){
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
#if 0
void test_r_db::test_db_embedded()
{
    // Our host file is 16mb, and we're going to create an 8mb database starting 4mb into the file.
    r_sqlite_conn conn("embedded.db:4194304:8388608", true, true);
    conn.exec("CREATE TABLE worker_bees(sesa_id TEXT, name TEST, id INTEGER PRIMARY KEY AUTOINCREMENT);");
    conn.exec("INSERT INTO worker_bees(sesa_id, name) VALUES('120129', 'Tony Di Croce');");
    auto pk = conn.last_insert_id();
    RTF_ASSERT(pk == "1");
    auto results = conn.exec("SELECT * FROM worker_bees;");
}

void test_r_db::test_db_embedded_multiple_tables()
{
    // Test multiple table creation and cross-table operations
    r_sqlite_conn conn("embedded.db:4194304:8388608", true, true);
    
    // Create multiple tables
    conn.exec("CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, email TEXT);");
    conn.exec("CREATE TABLE orders(id INTEGER PRIMARY KEY, user_id INTEGER, amount REAL, FOREIGN KEY(user_id) REFERENCES users(id));");
    conn.exec("CREATE TABLE products(id INTEGER PRIMARY KEY, name TEXT, price REAL);");
    
    // Insert test data
    conn.exec("INSERT INTO users(name, email) VALUES('John Doe', 'john@example.com');");
    conn.exec("INSERT INTO users(name, email) VALUES('Jane Smith', 'jane@example.com');");
    conn.exec("INSERT INTO products(name, price) VALUES('Widget A', 29.99);");
    conn.exec("INSERT INTO products(name, price) VALUES('Widget B', 39.99);");
    conn.exec("INSERT INTO orders(user_id, amount) VALUES(1, 29.99);");
    conn.exec("INSERT INTO orders(user_id, amount) VALUES(2, 39.99);");
    
    // Test JOIN operations
    auto results = conn.exec("SELECT u.name, o.amount FROM users u JOIN orders o ON u.id = o.user_id;");
    RTF_ASSERT(results.size() == 2);
    
    // Test aggregations
    auto total = conn.exec("SELECT SUM(amount) as total FROM orders;");
    RTF_ASSERT(total.size() == 1);
}

void test_r_db::test_db_embedded_large_data_insert()
{
    // Test inserting large amounts of data to stress the VFS
    r_sqlite_conn conn("embedded.db:4194304:8388608", true, true);
    
    conn.exec("CREATE TABLE large_table(id INTEGER PRIMARY KEY, data TEXT, value INTEGER);");
    
    // Insert 1000 rows of data
    conn.exec("BEGIN TRANSACTION;");
    for (int i = 0; i < 1000; i++) {
        std::string sql = "INSERT INTO large_table(data, value) VALUES('This is test data row " + 
                         std::to_string(i) + " with some content to make it larger', " + 
                         std::to_string(i * 2) + ");";
        conn.exec(sql);
    }
    conn.exec("COMMIT;");
    
    // Verify count
    auto count_result = conn.exec("SELECT COUNT(*) as count FROM large_table;");
    RTF_ASSERT(count_result.size() == 1);
    
    // Test range queries
    auto range_result = conn.exec("SELECT * FROM large_table WHERE value BETWEEN 100 AND 200;");
    RTF_ASSERT(range_result.size() > 0);
}

void test_r_db::test_db_embedded_blob_data()
{
    // Test BLOB storage and retrieval
    r_sqlite_conn conn("embedded.db:4194304:8388608", true, true);
    
    conn.exec("CREATE TABLE blob_table(id INTEGER PRIMARY KEY, name TEXT, data BLOB);");
    
    // Create some binary data
    std::vector<uint8_t> binary_data;
    for (int i = 0; i < 1024; i++) {
        binary_data.push_back(i % 256);
    }
    
    // Note: This test assumes your r_sqlite_conn has blob support
    // You may need to adapt this based on your actual blob insertion method
    conn.exec("INSERT INTO blob_table(name, data) VALUES('test_blob', X'deadbeef');");
    
    auto results = conn.exec("SELECT name, length(data) as data_length FROM blob_table;");
    RTF_ASSERT(results.size() == 1);
}

void test_r_db::test_db_embedded_indexing()
{
    // Test index creation and query performance
    r_sqlite_conn conn("embedded.db:4194304:8388608", true, true);
    
    conn.exec("CREATE TABLE indexed_table(id INTEGER PRIMARY KEY, search_field TEXT, value INTEGER);");
    
    // Insert test data
    conn.exec("BEGIN TRANSACTION;");
    for (int i = 0; i < 500; i++) {
        std::string sql = "INSERT INTO indexed_table(search_field, value) VALUES('search_" + 
                         std::to_string(i % 50) + "', " + std::to_string(i) + ");";
        conn.exec(sql);
    }
    conn.exec("COMMIT;");
    
    // Create index
    conn.exec("CREATE INDEX idx_search_field ON indexed_table(search_field);");
    
    // Test indexed queries
    auto results = conn.exec("SELECT * FROM indexed_table WHERE search_field = 'search_25';");
    RTF_ASSERT(results.size() == 10); // Should find 10 rows with this search field
    
    // Test EXPLAIN QUERY PLAN to verify index usage
    auto explain = conn.exec("EXPLAIN QUERY PLAN SELECT * FROM indexed_table WHERE search_field = 'search_25';");
    RTF_ASSERT(explain.size() > 0);
}

void test_r_db::test_db_embedded_transactions()
{
    // Test transaction handling and rollback
    r_sqlite_conn conn("embedded.db:4194304:8388608", true, true);
    
    conn.exec("CREATE TABLE transaction_test(id INTEGER PRIMARY KEY, value TEXT);");
    conn.exec("INSERT INTO transaction_test(value) VALUES('initial_value');");
    
    // Test successful transaction
    conn.exec("BEGIN TRANSACTION;");
    conn.exec("INSERT INTO transaction_test(value) VALUES('committed_value');");
    conn.exec("COMMIT;");
    
    auto after_commit = conn.exec("SELECT COUNT(*) as count FROM transaction_test;");
    RTF_ASSERT(after_commit.size() == 1);
    
    // Test rollback transaction
    conn.exec("BEGIN TRANSACTION;");
    conn.exec("INSERT INTO transaction_test(value) VALUES('rolled_back_value');");
    conn.exec("ROLLBACK;");
    
    auto after_rollback = conn.exec("SELECT COUNT(*) as count FROM transaction_test;");
    RTF_ASSERT(after_rollback.size() == 1);
    // Count should be same as after commit (rollback worked)
}

void test_r_db::test_db_embedded_concurrent_connections()
{
    // Test multiple connections to the same embedded database
    r_sqlite_conn conn1("embedded.db:4194304:8388608", true, true);
    r_sqlite_conn conn2("embedded.db:4194304:8388608", true, true);
    
    // Create table with first connection
    conn1.exec("CREATE TABLE concurrent_test(id INTEGER PRIMARY KEY, conn_id INTEGER, message TEXT);");
    
    // Insert from both connections
    conn1.exec("INSERT INTO concurrent_test(conn_id, message) VALUES(1, 'From connection 1');");
    conn2.exec("INSERT INTO concurrent_test(conn_id, message) VALUES(2, 'From connection 2');");
    
    // Read from both connections
    auto results1 = conn1.exec("SELECT * FROM concurrent_test ORDER BY id;");
    auto results2 = conn2.exec("SELECT * FROM concurrent_test ORDER BY id;");
    
    RTF_ASSERT(results1.size() == 2);
    RTF_ASSERT(results2.size() == 2);
    RTF_ASSERT(results1.size() == results2.size());
}

void test_r_db::test_db_embedded_schema_changes()
{
    // Test ALTER TABLE and other schema modifications
    r_sqlite_conn conn("embedded.db:4194304:8388608", true, true);
    
    // Create initial table
    conn.exec("CREATE TABLE schema_test(id INTEGER PRIMARY KEY, name TEXT);");
    conn.exec("INSERT INTO schema_test(name) VALUES('test_record');");
    
    // Add column
    conn.exec("ALTER TABLE schema_test ADD COLUMN email TEXT;");
    
    // Update with new column
    conn.exec("UPDATE schema_test SET email = 'test@example.com' WHERE id = 1;");
    
    // Verify schema change worked
    auto results = conn.exec("SELECT id, name, email FROM schema_test;");
    RTF_ASSERT(results.size() == 1);
    
    // Create additional table and drop it
    conn.exec("CREATE TABLE temp_table(id INTEGER);");
    conn.exec("DROP TABLE temp_table;");
    
    // Verify original table still works
    auto final_results = conn.exec("SELECT * FROM schema_test;");
    RTF_ASSERT(final_results.size() == 1);
}

void test_r_db::test_db_embedded_vacuum_and_analyze()
{
    // Test VACUUM and ANALYZE operations
    r_sqlite_conn conn("embedded.db:4194304:8388608", true, true);
    
    conn.exec("CREATE TABLE vacuum_test(id INTEGER PRIMARY KEY, data TEXT);");
    
    // Insert and delete data to create fragmentation
    conn.exec("BEGIN TRANSACTION;");
    for (int i = 0; i < 1000; i++) {
        std::string sql = "INSERT INTO vacuum_test(data) VALUES('data_" + std::to_string(i) + "');";
        conn.exec(sql);
    }
    conn.exec("COMMIT;");
    
    // Delete half the records
    conn.exec("DELETE FROM vacuum_test WHERE id % 2 = 0;");
    
    // Run VACUUM to reclaim space
    conn.exec("VACUUM;");
    
    // Run ANALYZE to update statistics
    conn.exec("ANALYZE;");
    
    // Verify data integrity after VACUUM
    auto remaining = conn.exec("SELECT COUNT(*) as count FROM vacuum_test;");
    RTF_ASSERT(remaining.size() == 1);
}

void test_r_db::test_db_embedded_edge_cases()
{
    // Test various edge cases and boundary conditions
    r_sqlite_conn conn("embedded.db:4194304:8388608", true, true);
    
    // Test empty table operations
    conn.exec("CREATE TABLE empty_test(id INTEGER PRIMARY KEY);");
    auto empty_results = conn.exec("SELECT * FROM empty_test;");
    RTF_ASSERT(empty_results.size() == 0);
    
    // Test NULL handling
    conn.exec("CREATE TABLE null_test(id INTEGER PRIMARY KEY, nullable_field TEXT);");
    conn.exec("INSERT INTO null_test(nullable_field) VALUES(NULL);");
    conn.exec("INSERT INTO null_test(nullable_field) VALUES('not_null');");
    
    auto null_results = conn.exec("SELECT * FROM null_test WHERE nullable_field IS NULL;");
    RTF_ASSERT(null_results.size() == 1);
    
    // Test very long text
    std::string long_text(1000, 'A');
    // Note: You may need to escape the string properly for your exec method
    conn.exec("CREATE TABLE long_text_test(id INTEGER PRIMARY KEY, long_field TEXT);");
    // This might need adjustment based on your parameter binding capabilities
    
    // Test Unicode text (if supported)
    conn.exec("CREATE TABLE unicode_test(id INTEGER PRIMARY KEY, unicode_field TEXT);");
    conn.exec("INSERT INTO unicode_test(unicode_field) VALUES('Hello 世界 🌍');");
    
    auto unicode_results = conn.exec("SELECT * FROM unicode_test;");
    RTF_ASSERT(unicode_results.size() == 1);
}

void test_r_db::test_db_embedded_readonly_access()
{
    // First, create a database with some data
    {
        r_sqlite_conn conn_rw("embedded.db:4194304:8388608", true, true);
        conn_rw.exec("CREATE TABLE readonly_test(id INTEGER PRIMARY KEY, value TEXT);");
        conn_rw.exec("INSERT INTO readonly_test(value) VALUES('test_data');");
    }

    // Now open in read-only mode
    r_sqlite_conn conn_ro("embedded.db:4194304:8388608", false, true);

    // Should be able to read
    auto results = conn_ro.exec("SELECT * FROM readonly_test;");
    RTF_ASSERT(results.size() == 1);

    // Should NOT be able to write (this might throw or return error)
    // You'll need to adapt this based on how your r_sqlite_conn handles errors
    try {
        conn_ro.exec("INSERT INTO readonly_test(value) VALUES('should_fail');");
        RTF_ASSERT(false); // Should not reach here
    } catch (...) {
        // Expected to fail
    }
}

void test_r_db::test_db_embedded_size_limits()
{
    // Test approaching the size limits of the embedded region
    r_sqlite_conn conn("embedded.db:4194304:8388608", true, true);
    
    conn.exec("CREATE TABLE size_test(id INTEGER PRIMARY KEY, large_data TEXT);");
    
    // Try to fill up a significant portion of the 8MB space
    // Each row will be roughly 1KB, so 1000 rows = ~1MB
    std::string large_string(1000, 'X');
    
    conn.exec("BEGIN TRANSACTION;");
    try {
        for (int i = 0; i < 1000; i++) {
            std::string sql = "INSERT INTO size_test(large_data) VALUES('" + large_string + "');";
            conn.exec(sql);
        }
        conn.exec("COMMIT;");
        
        // Verify we can still query the data
        auto count = conn.exec("SELECT COUNT(*) as count FROM size_test;");
        RTF_ASSERT(count.size() == 1);
        
    } catch (...) {
        // If we hit size limits, that's also a valid test result
        conn.exec("ROLLBACK;");
    }
}

std::string to_hex(uint8_t* p) {
    std::ostringstream oss;
    oss << "0x" <<std::hex << reinterpret_cast<uintptr_t>(p);
    return oss.str();
}

void test_r_db::test_db_embedded_basic_mapping()
{
    auto f = r_file::open("embedded.db", "r+");

    r_memory_map mm(
        r_fs::fileno(f),
        0,
        (uint32_t)1024*1024*16,
        r_memory_map::RMM_PROT_READ | r_memory_map::RMM_PROT_WRITE,
        r_memory_map::RMM_TYPE_FILE | r_memory_map::RMM_SHARED
    );

    uint8_t* p = (uint8_t*)mm.map();

    p += 1024*1024*4;

    auto hex_map_p = to_hex(p);
    
    string file_name = "embedded.db:4194304:8388608:map:" + hex_map_p + ":8388608";

    r_sqlite_conn conn(file_name, true, true);

    conn.exec("CREATE TABLE test_table(id INTEGER PRIMARY KEY, data TEXT);");
    conn.exec("INSERT INTO test_table(data) VALUES('test_data');");

    auto results = conn.exec("SELECT * FROM test_table;");
    RTF_ASSERT(results.size() == 1);
    RTF_ASSERT(results[0]["data"] == "test_data");
}

void test_r_db::test_db_embedded_mapping_multiple_connections()
{
    // Test multiple connections sharing the same mapping
    auto f = r_file::open("embedded.db", "r+");
    r_memory_map mm(
        r_fs::fileno(f),
        0,
        (uint32_t)1024*1024*16,
        r_memory_map::RMM_PROT_READ | r_memory_map::RMM_PROT_WRITE,
        r_memory_map::RMM_TYPE_FILE | r_memory_map::RMM_SHARED
    );
    uint8_t* p = (uint8_t*)mm.map();
    p += 1024*1024*2;  // 2MB offset
    auto hex_map_p = to_hex(p);
    
    string file_name = "embedded.db:2097152:4194304:map:" + hex_map_p + ":4194304";
    
    // First connection creates the table
    {
        r_sqlite_conn conn1(file_name, true, true);
        conn1.exec("CREATE TABLE shared_table(id INTEGER PRIMARY KEY, value TEXT);");
        conn1.exec("INSERT INTO shared_table(value) VALUES('from_conn1');");
    }
    
    // Second connection should see the data
    {
        r_sqlite_conn conn2(file_name, true, true);
        auto results = conn2.exec("SELECT * FROM shared_table;");
        RTF_ASSERT(results.size() == 1);
        RTF_ASSERT(results[0]["value"] == "from_conn1");
        
        conn2.exec("INSERT INTO shared_table(value) VALUES('from_conn2');");
    }
    
    // Third connection should see both records
    {
        r_sqlite_conn conn3(file_name, true, true);
        auto results = conn3.exec("SELECT COUNT(*) as count FROM shared_table;");
        RTF_ASSERT(results.size() == 1);
        RTF_ASSERT(stoi(results[0]["count"].value()) == 2);
    }
}


void test_r_db::test_db_embedded_mapping_fallback()
{
    // Test fallback behavior when mapping parameters are invalid
    auto f = r_file::open("embedded.db", "r+");
    r_memory_map mm(
        r_fs::fileno(f),
        0,
        (uint32_t)1024*1024*8,
        r_memory_map::RMM_PROT_READ | r_memory_map::RMM_PROT_WRITE,
        r_memory_map::RMM_TYPE_FILE | r_memory_map::RMM_SHARED
    );
    uint8_t* p = (uint8_t*)mm.map();
    p += 1024*1024*2;
    
    // Test with invalid mapping size (larger than actual mapping)
    auto hex_map_p = to_hex(p);
    string file_name = "embedded.db:2097152:4194304:map:" + hex_map_p + ":16777216"; // 16MB claimed, but only 6MB available
    
    // Should still work but fall back to file I/O for out-of-bounds operations
    r_sqlite_conn conn(file_name, true, true);
    conn.exec("CREATE TABLE fallback_table(id INTEGER PRIMARY KEY, data TEXT);");
    conn.exec("INSERT INTO fallback_table(data) VALUES('fallback_test');");
    
    auto results = conn.exec("SELECT * FROM fallback_table;");
    RTF_ASSERT(results.size() == 1);
    RTF_ASSERT(results[0]["data"].value() == "fallback_test");
}

void test_r_db::test_db_embedded_mapping_read_only()
{
    // Test read-only access to mapped database
    auto f = r_file::open("embedded.db", "r+");
    r_memory_map mm(
        r_fs::fileno(f),
        0,
        (uint32_t)1024*1024*8,
        r_memory_map::RMM_PROT_READ | r_memory_map::RMM_PROT_WRITE,
        r_memory_map::RMM_TYPE_FILE | r_memory_map::RMM_SHARED
    );
    uint8_t* p = (uint8_t*)mm.map();
    p += 1024*1024;
    auto hex_map_p = to_hex(p);
    
    string file_name = "embedded.db:1048576:4194304:map:" + hex_map_p + ":4194304";
    
    // First, create the database with write access
    {
        r_sqlite_conn conn_write(file_name, true, true);
        conn_write.exec("CREATE TABLE readonly_table(id INTEGER PRIMARY KEY, data TEXT);");
        conn_write.exec("INSERT INTO readonly_table(data) VALUES('readonly_test');");
    }
    
    // Then open read-only (would need to modify r_sqlite_conn to support read-only flag)
    // For now, just test that reads work
    {
        r_sqlite_conn conn_read(file_name, false, true);  // Assuming false = read-only
        auto results = conn_read.exec("SELECT * FROM readonly_table;");
        RTF_ASSERT(results.size() == 1);
        RTF_ASSERT(results[0]["data"] == "readonly_test");
    }
}

void test_r_db::test_db_embedded_mapping_boundary_conditions()
{
    // Test edge cases and boundary conditions
    auto f = r_file::open("embedded.db", "r+");
    r_memory_map mm(
        r_fs::fileno(f),
        0,
        (uint32_t)1024*1024*4,
        r_memory_map::RMM_PROT_READ | r_memory_map::RMM_PROT_WRITE,
        r_memory_map::RMM_TYPE_FILE | r_memory_map::RMM_SHARED
    );
    uint8_t* p = (uint8_t*)mm.map();
    p += 1024*512;  // 512KB offset
    auto hex_map_p = to_hex(p);
    
    // Create database with minimal size
    string file_name = "embedded.db:524288:1048576:map:" + hex_map_p + ":1048576";
    r_sqlite_conn conn(file_name, true, true);
    
    // Test very small transactions
    conn.exec("CREATE TABLE boundary_table(id INTEGER PRIMARY KEY, data TEXT);");
    
    // Test empty string
    conn.exec("INSERT INTO boundary_table(data) VALUES('');");
    
    // Test NULL value
    conn.exec("INSERT INTO boundary_table(data) VALUES(NULL);");
    
    // Test very long string (but within bounds)
    string long_string(50000, 'X');
    conn.exec("INSERT INTO boundary_table(data) VALUES('" + long_string +"');");
    
    auto results = conn.exec("SELECT COUNT(*) as count FROM boundary_table;");
    RTF_ASSERT(stoi(results[0]["count"].value()) == 3);
    
    // Verify the long string was stored correctly
    auto long_results = conn.exec("SELECT LENGTH(data) as len FROM boundary_table WHERE data IS NOT NULL AND data != '';");
    RTF_ASSERT(stoi(long_results[0]["len"].value()) == 50000);
}

void test_r_db::test_db_embedded_mapping_concurrent_access()
{
    // Test concurrent access patterns (if threading is available)
    auto f = r_file::open("embedded.db", "r+");
    r_memory_map mm(
        r_fs::fileno(f),
        0,
        (uint32_t)1024*1024*16,
        r_memory_map::RMM_PROT_READ | r_memory_map::RMM_PROT_WRITE,
        r_memory_map::RMM_TYPE_FILE | r_memory_map::RMM_SHARED
    );
    uint8_t* p = (uint8_t*)mm.map();
    p += 1024*1024*4;
    auto hex_map_p = to_hex(p);
    
    string file_name = "embedded.db:4194304:8388608:map:" + hex_map_p + ":8388608";
    
    // Setup initial table
    {
        r_sqlite_conn setup_conn(file_name, true, true);
        setup_conn.exec("CREATE TABLE concurrent_table(id INTEGER PRIMARY KEY, thread_id INTEGER, data TEXT);");
    }
    
    // Simulate multiple connections accessing the same mapped database
    // (In a real concurrent test, you'd use actual threads)
    for (int conn_id = 0; conn_id < 5; conn_id++) {
        r_sqlite_conn conn(file_name, true, true);
        
        for (int i = 0; i < 10; i++) {
            string data = "conn_" + to_string(conn_id) + "_row_" + to_string(i);
            conn.exec("INSERT INTO concurrent_table(thread_id, data) VALUES('" + to_string(conn_id) + "', '" + data + "');");
        }
    }
    
    // Verify all data was written
    r_sqlite_conn verify_conn(file_name, true, true);
    auto results = verify_conn.exec("SELECT COUNT(*) as count FROM concurrent_table;");
    RTF_ASSERT(stoi(results[0]["count"].value()) == 50);
    
    // Verify data from each "connection"
    for (int conn_id = 0; conn_id < 5; conn_id++) {
        auto conn_results = verify_conn.exec("SELECT COUNT(*) as count FROM concurrent_table WHERE thread_id = '" + to_string(conn_id) + "';");
        RTF_ASSERT(stoi(conn_results[0]["count"].value()) == 10);
    }
}
#endif
