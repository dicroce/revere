
#include "framework.h"

class test_r_db : public test_fixture
{
public:
    RTF_FIXTURE(test_r_db);
      TEST(test_r_db::test_db_ctor);
      TEST(test_r_db::test_db_insert);
      TEST(test_r_db::test_db_paging);
    RTF_FIXTURE_END();

    virtual ~test_r_db() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_db_ctor();
    void test_db_insert();
    void test_db_paging();
};
