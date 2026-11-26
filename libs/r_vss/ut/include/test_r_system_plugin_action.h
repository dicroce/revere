#include "framework.h"

class test_r_system_plugin_action : public test_fixture
{
public:
    RTF_FIXTURE(test_r_system_plugin_action);
      TEST(test_r_system_plugin_action::test_runs_the_plugins_action);
      TEST(test_r_system_plugin_action::test_no_export_or_no_plugin);
      TEST(test_r_system_plugin_action::test_queued_action_waits_for_the_ui_thread);
      TEST(test_r_system_plugin_action::test_nothing_queued_without_an_action_export);
    RTF_FIXTURE_END();

    virtual ~test_r_system_plugin_action() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_runs_the_plugins_action();
    void test_no_export_or_no_plugin();
    void test_queued_action_waits_for_the_ui_thread();
    void test_nothing_queued_without_an_action_export();

private:
    std::string _top;
};
