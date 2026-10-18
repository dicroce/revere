#include "framework.h"

class test_r_onvif_event_tracker : public test_fixture
{
public:
    RTF_FIXTURE(test_r_onvif_event_tracker);
      TEST(test_r_onvif_event_tracker::test_normalize_topic);
      TEST(test_r_onvif_event_tracker::test_reolink_motion_and_person);
      TEST(test_r_onvif_event_tracker::test_axis_burst_uses_one_topic);
      TEST(test_r_onvif_event_tracker::test_topic_upgrade_mid_motion);
      TEST(test_r_onvif_event_tracker::test_initialized_corrects_state);
      TEST(test_r_onvif_event_tracker::test_sources_stay_apart);
      TEST(test_r_onvif_event_tracker::test_close_all);
      TEST(test_r_onvif_event_tracker::test_unknown_topics_ignored);
    RTF_FIXTURE_END();

    virtual ~test_r_onvif_event_tracker() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_normalize_topic();
    void test_reolink_motion_and_person();
    void test_axis_burst_uses_one_topic();
    void test_topic_upgrade_mid_motion();
    void test_initialized_corrects_state();
    void test_sources_stay_apart();
    void test_close_all();
    void test_unknown_topics_ignored();
};
