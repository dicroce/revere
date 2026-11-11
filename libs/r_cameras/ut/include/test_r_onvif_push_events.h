#include "framework.h"

// r_onvif_events with a camera that has no pull point: a fake ONVIF camera on loopback that takes Subscribe, Renew and
// Unsubscribe, and the test posts its Notify messages to the address it was given.
class test_r_onvif_push_events : public test_fixture
{
public:
    RTF_FIXTURE(test_r_onvif_push_events);
      TEST(test_r_onvif_push_events::test_push_when_the_camera_has_no_pull_point);
      TEST(test_r_onvif_push_events::test_push_when_pull_point_is_not_supported);
      TEST(test_r_onvif_push_events::test_pull_point_is_used_when_there_is_one);
      TEST(test_r_onvif_push_events::test_subscribe_not_supported);
      TEST(test_r_onvif_push_events::test_junk_notifications_are_dropped);
      TEST(test_r_onvif_push_events::test_the_subscription_is_renewed);
      TEST(test_r_onvif_push_events::test_a_camera_that_posts_to_the_root_and_grants_less);
      TEST(test_r_onvif_push_events::test_a_heartbeat_that_stops_resubscribes);
      TEST(test_r_onvif_push_events::test_a_camera_that_only_sends_changes_is_left_alone);
    RTF_FIXTURE_END();

    virtual ~test_r_onvif_push_events() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_push_when_the_camera_has_no_pull_point();
    void test_push_when_pull_point_is_not_supported();
    void test_pull_point_is_used_when_there_is_one();
    void test_subscribe_not_supported();
    void test_junk_notifications_are_dropped();
    void test_the_subscription_is_renewed();
    void test_a_camera_that_posts_to_the_root_and_grants_less();
    void test_a_heartbeat_that_stops_resubscribes();
    void test_a_camera_that_only_sends_changes_is_left_alone();
};
