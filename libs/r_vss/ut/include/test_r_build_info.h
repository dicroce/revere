#include "framework.h"

class test_r_build_info : public test_fixture
{
public:
    RTF_FIXTURE(test_r_build_info);
      TEST(test_r_build_info::test_version_and_build_number);
      TEST(test_r_build_info::test_build_time_is_utc);
      TEST(test_r_build_info::test_summary);
    RTF_FIXTURE_END();

    virtual ~test_r_build_info() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_version_and_build_number();
    void test_build_time_is_utc();
    void test_summary();
};
