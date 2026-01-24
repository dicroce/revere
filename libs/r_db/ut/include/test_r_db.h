
#include "framework.h"

class test_r_db : public test_fixture
{
public:
    RTF_FIXTURE(test_r_db);
      TEST(test_r_db::test_db_ctor);
      TEST(test_r_db::test_db_insert);
      TEST(test_r_db::test_db_paging);
#if 0
      TEST(test_r_db::test_db_embedded);
      TEST(test_r_db::test_db_embedded_multiple_tables);
      TEST(test_r_db::test_db_embedded_large_data_insert);
      TEST(test_r_db::test_db_embedded_blob_data);
      TEST(test_r_db::test_db_embedded_indexing);
      TEST(test_r_db::test_db_embedded_transactions);
      TEST(test_r_db::test_db_embedded_concurrent_connections);
      TEST(test_r_db::test_db_embedded_schema_changes);
      TEST(test_r_db::test_db_embedded_vacuum_and_analyze);
      TEST(test_r_db::test_db_embedded_edge_cases);
      TEST(test_r_db::test_db_embedded_readonly_access);
      TEST(test_r_db::test_db_embedded_size_limits);
      TEST(test_r_db::test_db_embedded_basic_mapping);
      TEST(test_r_db::test_db_embedded_mapping_multiple_connections);
      TEST(test_r_db::test_db_embedded_mapping_fallback);
      TEST(test_r_db::test_db_embedded_mapping_read_only);
      TEST(test_r_db::test_db_embedded_mapping_boundary_conditions);
      TEST(test_r_db::test_db_embedded_mapping_concurrent_access);
#endif
    RTF_FIXTURE_END();

    virtual ~test_r_db() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_db_ctor();
    void test_db_insert();
    void test_db_paging();
#if 0
    void test_db_embedded();
    void test_db_embedded_multiple_tables();
    void test_db_embedded_large_data_insert();
    void test_db_embedded_blob_data();
    void test_db_embedded_indexing();
    void test_db_embedded_transactions();
    void test_db_embedded_concurrent_connections();
    void test_db_embedded_schema_changes();
    void test_db_embedded_vacuum_and_analyze();
    void test_db_embedded_edge_cases();
    void test_db_embedded_readonly_access();
    void test_db_embedded_size_limits();
    void test_db_embedded_basic_mapping();
    void test_db_embedded_mapping_multiple_connections();
    void test_db_embedded_mapping_fallback();
    void test_db_embedded_mapping_read_only();
    void test_db_embedded_mapping_boundary_conditions();
    void test_db_embedded_mapping_concurrent_access();
#endif
};
