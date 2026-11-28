#include "framework.h"

// r_camera_events_keeper: a camera's event subscription kept across its stream's reconnects, with fake events.
class test_r_camera_events_keeper : public test_fixture
{
public:
    RTF_FIXTURE(test_r_camera_events_keeper);
      TEST(test_r_camera_events_keeper::test_a_reconnect_keeps_the_subscription);
      TEST(test_r_camera_events_keeper::test_attach_is_told_what_is_open_and_detach_that_it_ended);
      TEST(test_r_camera_events_keeper::test_transitions_reach_the_attached_and_always);
      TEST(test_r_camera_events_keeper::test_new_settings_make_a_new_subscription);
      TEST(test_r_camera_events_keeper::test_a_camera_that_goes_ends_its_subscription);
      TEST(test_r_camera_events_keeper::test_only_the_attached_owner_detaches);
      TEST(test_r_camera_events_keeper::test_a_camera_without_events_has_none);
      TEST(test_r_camera_events_keeper::test_new_settings_with_no_events_still_end_the_old_one);
    RTF_FIXTURE_END();

    virtual ~test_r_camera_events_keeper() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_a_reconnect_keeps_the_subscription();
    void test_attach_is_told_what_is_open_and_detach_that_it_ended();
    void test_transitions_reach_the_attached_and_always();
    void test_new_settings_make_a_new_subscription();
    void test_a_camera_that_goes_ends_its_subscription();
    void test_only_the_attached_owner_detaches();
    void test_a_camera_without_events_has_none();
    void test_new_settings_with_no_events_still_end_the_old_one();
};
