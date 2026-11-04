
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
};
