#include "framework.h"

class test_r_storage_summary : public test_fixture
{
public:
    RTF_FIXTURE(test_r_storage_summary);
      TEST(test_r_storage_summary::test_no_cameras);
      TEST(test_r_storage_summary::test_sums_and_days);
      TEST(test_r_storage_summary::test_main_dir_is_the_one_with_most_reserved);
      TEST(test_r_storage_summary::test_idle_and_empty_cameras_have_no_days);
    RTF_FIXTURE_END();

    virtual ~test_r_storage_summary() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_no_cameras();
    void test_sums_and_days();
    void test_main_dir_is_the_one_with_most_reserved();
    void test_idle_and_empty_cameras_have_no_days();
};
