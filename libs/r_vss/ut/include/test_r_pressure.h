#include "framework.h"

class test_r_pressure : public test_fixture
{
public:
    RTF_FIXTURE(test_r_pressure);
      TEST(test_r_pressure::test_nothing_known_is_ok_and_lists_every_signal_unknown);
      TEST(test_r_pressure::test_motion_signal);
      TEST(test_r_pressure::test_detection_signal);
      TEST(test_r_pressure::test_write_signal);
      TEST(test_r_pressure::test_cpu_signal);
      TEST(test_r_pressure::test_cameras_behind_count_only_while_the_cpu_is_busy);
      TEST(test_r_pressure::test_hardware_encoder_signal);
      TEST(test_r_pressure::test_level_is_the_worst_and_every_reason_is_listed);
      TEST(test_r_pressure::test_cpu_sampler_average_and_holds);
      TEST(test_r_pressure::test_cpu_sampler_ignores_resets_and_stalls);
      TEST(test_r_pressure::test_cpu_sampler_day_percentile);
      TEST(test_r_pressure::test_monitor_cpu_to_level);
      TEST(test_r_pressure::test_monitor_motion_notes_and_queue);
      TEST(test_r_pressure::test_monitor_write_delay_growing);
      TEST(test_r_pressure::test_monitor_detection_against_its_baseline);
      TEST(test_r_pressure::test_monitor_detection_needs_a_baseline_first);
      TEST(test_r_pressure::test_monitor_json);
      TEST(test_r_pressure::test_write_delay_recorder);
      TEST(test_r_pressure::test_real_cpu_reading);
    RTF_FIXTURE_END();

    virtual ~test_r_pressure() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_nothing_known_is_ok_and_lists_every_signal_unknown();
    void test_motion_signal();
    void test_detection_signal();
    void test_write_signal();
    void test_cpu_signal();
    void test_cameras_behind_count_only_while_the_cpu_is_busy();
    void test_hardware_encoder_signal();
    void test_level_is_the_worst_and_every_reason_is_listed();
    void test_cpu_sampler_average_and_holds();
    void test_cpu_sampler_ignores_resets_and_stalls();
    void test_cpu_sampler_day_percentile();
    void test_monitor_cpu_to_level();
    void test_monitor_motion_notes_and_queue();
    void test_monitor_write_delay_growing();
    void test_monitor_detection_against_its_baseline();
    void test_monitor_detection_needs_a_baseline_first();
    void test_monitor_json();
    void test_write_delay_recorder();
    void test_real_cpu_reading();
};
