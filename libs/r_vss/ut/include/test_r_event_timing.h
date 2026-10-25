#include "framework.h"

class test_r_event_timing : public test_fixture
{
public:
    RTF_FIXTURE(test_r_event_timing);
      TEST(test_r_event_timing::test_json_members);
      TEST(test_r_event_timing::test_unknown_times_left_out);
      TEST(test_r_event_timing::test_looks_capped);
      TEST(test_r_event_timing::test_post_detection_prefers_v2);
      TEST(test_r_event_timing::test_snapshot_reports_key_frame_ts);
    RTF_FIXTURE_END();

    virtual ~test_r_event_timing() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_json_members();
    void test_unknown_times_left_out();
    void test_looks_capped();
    void test_post_detection_prefers_v2();
    void test_snapshot_reports_key_frame_ts();
};
