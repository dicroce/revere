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
      TEST(test_r_system_plugin_motion::test_a_plugin_loaded_after_start_gets_each_cameras_latest_state);
      TEST(test_r_system_plugin_motion::test_stop_all_waits_for_a_call_in_the_plugin);
      TEST(test_r_system_plugin_motion::test_stop_all_gives_up_on_a_call_that_does_not_return);
      TEST(test_r_system_plugin_motion::test_destroying_the_host_leaves_a_plugin_in_a_call_loaded);
      TEST(test_r_system_plugin_motion::test_destroying_the_host_leaves_a_plugin_in_a_detection_loaded);
      TEST(test_r_system_plugin_motion::test_a_late_detection_does_not_go_on_to_the_next_plugin);
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
    void test_a_plugin_loaded_after_start_gets_each_cameras_latest_state();
    void test_stop_all_waits_for_a_call_in_the_plugin();
    void test_stop_all_gives_up_on_a_call_that_does_not_return();
    void test_destroying_the_host_leaves_a_plugin_in_a_call_loaded();
    void test_destroying_the_host_leaves_a_plugin_in_a_detection_loaded();
    void test_a_late_detection_does_not_go_on_to_the_next_plugin();

private:
    std::string _top;
};
