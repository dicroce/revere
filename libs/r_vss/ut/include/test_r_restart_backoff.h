#include "framework.h"

class test_r_restart_backoff : public test_fixture
{
public:
    RTF_FIXTURE(test_r_restart_backoff);
      TEST(test_r_restart_backoff::test_new_camera_starts_at_once);
      TEST(test_r_restart_backoff::test_stream_that_had_video_restarts_at_once);
      TEST(test_r_restart_backoff::test_no_video_waits_longer_each_time);
      TEST(test_r_restart_backoff::test_first_frame_resets_the_wait);
      TEST(test_r_restart_backoff::test_new_settings_start_at_once);
      TEST(test_r_restart_backoff::test_no_answer_retries_quickly_at_first);
      TEST(test_r_restart_backoff::test_short_sessions_wait_longer_each_time);
      TEST(test_r_restart_backoff::test_short_sessions_and_failures_mix);
    RTF_FIXTURE_END();

    virtual ~test_r_restart_backoff() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_new_camera_starts_at_once();
    void test_stream_that_had_video_restarts_at_once();
    void test_no_video_waits_longer_each_time();
    void test_first_frame_resets_the_wait();
    void test_new_settings_start_at_once();
    void test_no_answer_retries_quickly_at_first();
    void test_short_sessions_wait_longer_each_time();
    void test_short_sessions_and_failures_mix();
};
