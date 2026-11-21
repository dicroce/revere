#include "framework.h"

class test_r_export_output : public test_fixture
{
public:
    RTF_FIXTURE(test_r_export_output);
      TEST(test_r_export_output::test_header_failure_fails_and_cleans_up);
      TEST(test_r_export_output::test_finished_export_is_renamed);
      TEST(test_r_export_output::test_unfinished_export_is_removed);
    RTF_FIXTURE_END();

    virtual ~test_r_export_output() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_header_failure_fails_and_cleans_up();
    void test_finished_export_is_renamed();
    void test_unfinished_export_is_removed();
};
