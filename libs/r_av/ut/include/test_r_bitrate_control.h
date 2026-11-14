#include "framework.h"

class test_r_bitrate_control : public test_fixture
{
public:
    RTF_FIXTURE(test_r_bitrate_control);
      TEST(test_r_bitrate_control::test_the_first_window_is_not_judged);
      TEST(test_r_bitrate_control::test_an_overshoot_cuts_the_target_by_30_percent_to_the_floor);
      TEST(test_r_bitrate_control::test_an_undershoot_raises_the_target_by_25_percent_to_the_cap);
      TEST(test_r_bitrate_control::test_a_target_near_what_is_produced_is_left_alone);
      TEST(test_r_bitrate_control::test_a_restart_skips_its_first_window_and_keeps_the_target);
      TEST(test_r_bitrate_control::test_the_rate_is_over_the_time_that_elapsed);
    RTF_FIXTURE_END();

    virtual ~test_r_bitrate_control() throw() {}
    virtual void setup() {}
    virtual void teardown() {}

    void test_the_first_window_is_not_judged();
    void test_an_overshoot_cuts_the_target_by_30_percent_to_the_floor();
    void test_an_undershoot_raises_the_target_by_25_percent_to_the_cap();
    void test_a_target_near_what_is_produced_is_left_alone();
    void test_a_restart_skips_its_first_window_and_keeps_the_target();
    void test_the_rate_is_over_the_time_that_elapsed();
};
