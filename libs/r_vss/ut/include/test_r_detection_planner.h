#include "framework.h"

class test_r_detection_planner : public test_fixture
{
public:
    RTF_FIXTURE(test_r_detection_planner);
      TEST(test_r_detection_planner::test_first_look_then_follow_ups);
      TEST(test_r_detection_planner::test_short_event_uses_last_key_frame);
      TEST(test_r_detection_planner::test_events_cluster_into_one_session);
      TEST(test_r_detection_planner::test_far_apart_events_are_separate);
      TEST(test_r_detection_planner::test_pre_roll_drops_parked_objects);
      TEST(test_r_detection_planner::test_first_and_last_frame_static_check);
      TEST(test_r_detection_planner::test_failed_stored_look_retries_once);
      TEST(test_r_detection_planner::test_fair_between_cameras);
      TEST(test_r_detection_planner::test_early_camera_class);
      TEST(test_r_detection_planner::test_sources);
      TEST(test_r_detection_planner::test_look_cap);
    RTF_FIXTURE_END();

    virtual ~test_r_detection_planner() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_first_look_then_follow_ups();
    void test_short_event_uses_last_key_frame();
    void test_events_cluster_into_one_session();
    void test_far_apart_events_are_separate();
    void test_pre_roll_drops_parked_objects();
    void test_first_and_last_frame_static_check();
    void test_failed_stored_look_retries_once();
    void test_fair_between_cameras();
    void test_early_camera_class();
    void test_sources();
    void test_look_cap();
};
