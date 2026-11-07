#include "framework.h"

class test_r_failure_reason : public test_fixture
{
public:
    RTF_FIXTURE(test_r_failure_reason);
      TEST(test_r_failure_reason::test_first_line_only);
      TEST(test_r_failure_reason::test_source_position_dropped);
      TEST(test_r_failure_reason::test_other_paths_cut_to_last_part);
    RTF_FIXTURE_END();

    virtual ~test_r_failure_reason() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_first_line_only();
    void test_source_position_dropped();
    void test_other_paths_cut_to_last_part();
};
