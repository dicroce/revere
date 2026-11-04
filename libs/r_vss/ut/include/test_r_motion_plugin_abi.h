
#include "framework.h"

class test_r_motion_plugin_abi : public test_fixture
{
public:
    RTF_FIXTURE(test_r_motion_plugin_abi);
      TEST(test_r_motion_plugin_abi::test_a_matching_plugin_loads);
      TEST(test_r_motion_plugin_abi::test_a_stale_plugin_is_refused);
    RTF_FIXTURE_END();

    virtual ~test_r_motion_plugin_abi() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_a_matching_plugin_loads();
    void test_a_stale_plugin_is_refused();
};
