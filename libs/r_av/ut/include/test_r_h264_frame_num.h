
#include "framework.h"

class test_r_h264_frame_num : public test_fixture
{
public:
    RTF_FIXTURE(test_r_h264_frame_num);
      TEST(test_r_h264_frame_num::test_in_order_with_wrap);
      TEST(test_r_h264_frame_num::test_lost_picture_is_a_gap);
      TEST(test_r_h264_frame_num::test_lost_idr_is_a_gap);
      TEST(test_r_h264_frame_num::test_non_reference_pictures);
      TEST(test_r_h264_frame_num::test_multi_slice_counts_once);
      TEST(test_r_h264_frame_num::test_sps_change_waits_for_idr);
      TEST(test_r_h264_frame_num::test_gaps_allowed);
      TEST(test_r_h264_frame_num::test_parameter_sets_kept);
      TEST(test_r_h264_frame_num::test_high_profile_scaling_matrix);
      TEST(test_r_h264_frame_num::test_emulation_prevention);
      TEST(test_r_h264_frame_num::test_real_encoder_stream);
      TEST(test_r_h264_frame_num::test_videotoolbox_gap_handling);
    RTF_FIXTURE_END();

    virtual ~test_r_h264_frame_num() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_in_order_with_wrap();
    void test_lost_picture_is_a_gap();
    void test_lost_idr_is_a_gap();
    void test_non_reference_pictures();
    void test_multi_slice_counts_once();
    void test_sps_change_waits_for_idr();
    void test_gaps_allowed();
    void test_parameter_sets_kept();
    void test_high_profile_scaling_matrix();
    void test_emulation_prevention();
    void test_real_encoder_stream();
    void test_videotoolbox_gap_handling();
};
