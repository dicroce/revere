#include "framework.h"

class test_r_system_plugin_dirs : public test_fixture
{
public:
    RTF_FIXTURE(test_r_system_plugin_dirs);
      TEST(test_r_system_plugin_dirs::test_not_a_snap);
      TEST(test_r_system_plugin_dirs::test_snap);
      TEST(test_r_system_plugin_dirs::test_snap_without_user_common);
    RTF_FIXTURE_END();

    virtual ~test_r_system_plugin_dirs() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_not_a_snap();
    void test_snap();
    void test_snap_without_user_common();
};
