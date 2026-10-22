#include "framework.h"

class test_log_tail : public test_fixture
{
public:
    RTF_FIXTURE(test_log_tail);
      TEST(test_log_tail::test_keeps_last_lines);
      TEST(test_log_tail::test_counts_dropped_lines);
    RTF_FIXTURE_END();

    virtual ~test_log_tail() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_keeps_last_lines();
    void test_counts_dropped_lines();
};
