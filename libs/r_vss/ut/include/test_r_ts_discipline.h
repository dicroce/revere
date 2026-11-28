#include "framework.h"

class test_r_ts_discipline : public test_fixture
{
public:
    RTF_FIXTURE(test_r_ts_discipline);
      TEST(test_r_ts_discipline::test_fast_camera_clock);
      TEST(test_r_ts_discipline::test_slow_camera_clock);
      TEST(test_r_ts_discipline::test_jitter);
      TEST(test_r_ts_discipline::test_frame_spacing);
      TEST(test_r_ts_discipline::test_stall_then_backlog);
      TEST(test_r_ts_discipline::test_connect_burst);
      TEST(test_r_ts_discipline::test_clock_jumps_back);
      TEST(test_r_ts_discipline::test_clock_jumps_forward);
      TEST(test_r_ts_discipline::test_audio_correction);
      TEST(test_r_ts_discipline::test_late_frames_capped);
      TEST(test_r_ts_discipline::test_late_frames_while_backed_up);
      TEST(test_r_ts_discipline::test_earlier_while_backed_up);
      TEST(test_r_ts_discipline::test_backed_up_window);
      TEST(test_r_ts_discipline::test_drift_reported);
      TEST(test_r_ts_discipline::test_drift_warnings);
      TEST(test_r_ts_discipline::test_drift_after_jump);
      TEST(test_r_ts_discipline::test_drift_message);
      TEST(test_r_ts_discipline::test_recording_clock_json);
    RTF_FIXTURE_END();

    virtual ~test_r_ts_discipline() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_fast_camera_clock();
    void test_slow_camera_clock();
    void test_jitter();
    void test_frame_spacing();
    void test_stall_then_backlog();
    void test_connect_burst();
    void test_clock_jumps_back();
    void test_clock_jumps_forward();
    void test_audio_correction();
    void test_late_frames_capped();
    void test_late_frames_while_backed_up();
    void test_earlier_while_backed_up();
    void test_backed_up_window();
    void test_drift_reported();
    void test_drift_warnings();
    void test_drift_after_jump();
    void test_drift_message();
    void test_recording_clock_json();
};
