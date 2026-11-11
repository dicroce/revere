
#include "framework.h"

class test_r_codec : public test_fixture
{
public:
    RTF_FIXTURE(test_r_codec);
      TEST(test_r_codec::test_basic_video_decode);
      TEST(test_r_codec::test_basic_video_transcode);
      TEST(test_r_codec::test_mjpeg_snapshot_quality);
      TEST(test_r_codec::test_decode_input_ending_at_a_page);
      TEST(test_r_codec::test_decode_again_after_its_input_sends_nothing);
      TEST(test_r_codec::test_hw_device_failure_decodes_in_software);
      TEST(test_r_codec::test_hw_accel_without_a_decoder_decodes_in_software);
      TEST(test_r_codec::test_hw_encoder_open_failure_encodes_in_software);
      TEST(test_r_codec::test_hw_encoder_missing_encodes_in_software);
      TEST(test_r_codec::test_software_encoder_failure_still_throws);
      TEST(test_r_codec::test_hw_decode_failure_is_remembered);
      TEST(test_r_codec::test_hw_failure_on_one_format_keeps_others_in_hardware);
      TEST(test_r_codec::test_dead_hw_context_falls_back_to_software);
      TEST(test_r_codec::test_software_decoder_does_not_fall_back);
      TEST(test_r_codec::test_hw_failures_not_in_a_row_keep_the_hardware);
      TEST(test_r_codec::test_hw_decoder_that_wont_open_decodes_in_software);
    RTF_FIXTURE_END();

    virtual ~test_r_codec() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_basic_video_decode();
    void test_basic_video_transcode();
    void test_mjpeg_snapshot_quality();
    void test_decode_input_ending_at_a_page();
    void test_decode_again_after_its_input_sends_nothing();
    void test_hw_device_failure_decodes_in_software();
    void test_hw_accel_without_a_decoder_decodes_in_software();
    void test_hw_encoder_open_failure_encodes_in_software();
    void test_hw_encoder_missing_encodes_in_software();
    void test_software_encoder_failure_still_throws();
    void test_hw_decode_failure_is_remembered();
    void test_hw_failure_on_one_format_keeps_others_in_hardware();
    void test_dead_hw_context_falls_back_to_software();
    void test_software_decoder_does_not_fall_back();
    void test_hw_failures_not_in_a_row_keep_the_hardware();
    void test_hw_decoder_that_wont_open_decodes_in_software();
};
