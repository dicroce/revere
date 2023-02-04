
#include "framework.h"

class test_r_motion : public test_fixture
{
public:
    RTF_FIXTURE(test_r_motion);
      TEST(test_r_motion::test_basic_utils);
    RTF_FIXTURE_END();

    virtual ~test_r_motion() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_basic_utils();
};
