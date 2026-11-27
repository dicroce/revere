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
};
