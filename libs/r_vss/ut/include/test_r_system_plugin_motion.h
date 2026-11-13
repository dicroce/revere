#include "framework.h"

class test_r_system_plugin_motion : public test_fixture
{
public:
    RTF_FIXTURE(test_r_system_plugin_motion);
      TEST(test_r_system_plugin_motion::test_plugin_gets_motion_start_end_and_state_changes);
      TEST(test_r_system_plugin_motion::test_a_plugin_started_later_gets_each_cameras_latest_state);
      TEST(test_r_system_plugin_motion::test_a_forgotten_camera_is_not_replayed);
      TEST(test_r_system_plugin_motion::test_nothing_before_start_or_after_stop);
      TEST(test_r_system_plugin_motion::test_a_plugin_without_the_exports_loads_and_is_left_alone);
      TEST(test_r_system_plugin_motion::test_a_slow_plugin_does_not_hold_up_the_caller);
    RTF_FIXTURE_END();

    virtual ~test_r_system_plugin_motion() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_plugin_gets_motion_start_end_and_state_changes();
    void test_a_plugin_started_later_gets_each_cameras_latest_state();
    void test_a_forgotten_camera_is_not_replayed();
    void test_nothing_before_start_or_after_stop();
    void test_a_plugin_without_the_exports_loads_and_is_left_alone();
    void test_a_slow_plugin_does_not_hold_up_the_caller();

private:
    std::string _top;
};
