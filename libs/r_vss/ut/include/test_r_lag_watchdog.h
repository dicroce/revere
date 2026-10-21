#include "framework.h"

class test_r_lag_watchdog : public test_fixture
{
public:
    RTF_FIXTURE(test_r_lag_watchdog);
      TEST(test_r_lag_watchdog::test_live_stream_with_jitter);
      TEST(test_r_lag_watchdog::test_slow_camera_crystal);
      TEST(test_r_lag_watchdog::test_backlog_lags_then_reconnects);
      TEST(test_r_lag_watchdog::test_stuck_behind_reconnects);
      TEST(test_r_lag_watchdog::test_blips_that_drain);
      TEST(test_r_lag_watchdog::test_stuck_over_2s_reconnects);
      TEST(test_r_lag_watchdog::test_camera_clock_jumps);
      TEST(test_r_lag_watchdog::test_b_frame_order);
      TEST(test_r_lag_watchdog::test_connect_burst);
      TEST(test_r_lag_watchdog::test_caught_up);
      TEST(test_r_lag_watchdog::test_rate);
      TEST(test_r_lag_watchdog::test_reconnect_backoff);
      TEST(test_r_lag_watchdog::test_reconnect_flag);
      TEST(test_r_lag_watchdog::test_newest_frame_gate);
    RTF_FIXTURE_END();

    virtual ~test_r_lag_watchdog() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_live_stream_with_jitter();
    void test_slow_camera_crystal();
    void test_backlog_lags_then_reconnects();
    void test_stuck_behind_reconnects();
    void test_blips_that_drain();
    void test_stuck_over_2s_reconnects();
    void test_camera_clock_jumps();
    void test_b_frame_order();
    void test_connect_burst();
    void test_caught_up();
    void test_rate();
    void test_reconnect_backoff();
    void test_reconnect_flag();
    void test_newest_frame_gate();
};
