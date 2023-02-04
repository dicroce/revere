
#include "framework.h"

class test_r_vss : public test_fixture
{
public:
    RTF_FIXTURE(test_r_vss);
      TEST(test_r_vss::test_r_stream_keeper_basic_recording);
    RTF_FIXTURE_END();

    virtual ~test_r_vss() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_r_stream_keeper_basic_recording();
};
