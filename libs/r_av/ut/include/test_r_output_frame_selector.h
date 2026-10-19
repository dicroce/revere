#include "framework.h"

class test_r_output_frame_selector : public test_fixture
{
public:
    RTF_FIXTURE(test_r_output_frame_selector);
      TEST(test_r_output_frame_selector::test_30_to_15_keeps_every_other_frame);
      TEST(test_r_output_frame_selector::test_30_to_15_millisecond_timestamps);
      TEST(test_r_output_frame_selector::test_jitter_fills_every_tick_once);
      TEST(test_r_output_frame_selector::test_gap_repeats_previous_frame);
      TEST(test_r_output_frame_selector::test_long_gap_jumps_without_repeats);
      TEST(test_r_output_frame_selector::test_reset_starts_over);
      TEST(test_r_output_frame_selector::test_hw_decode_get_only_kept_frames);
    RTF_FIXTURE_END();

    virtual ~test_r_output_frame_selector() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_30_to_15_keeps_every_other_frame();
    void test_30_to_15_millisecond_timestamps();
    void test_jitter_fills_every_tick_once();
    void test_gap_repeats_previous_frame();
    void test_long_gap_jumps_without_repeats();
    void test_reset_starts_over();
    void test_hw_decode_get_only_kept_frames();
};
