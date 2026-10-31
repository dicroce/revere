#include "framework.h"

class test_r_system_plugin_action : public test_fixture
{
public:
    RTF_FIXTURE(test_r_system_plugin_action);
      TEST(test_r_system_plugin_action::test_runs_the_plugins_action);
      TEST(test_r_system_plugin_action::test_no_export_or_no_plugin);
    RTF_FIXTURE_END();

    virtual ~test_r_system_plugin_action() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_runs_the_plugins_action();
    void test_no_export_or_no_plugin();

private:
    std::string _top;
};
