
#include "framework.h"

class test_r_pipeline : public test_fixture
{
public:
    RTF_FIXTURE(test_r_pipeline);
      TEST(test_r_pipeline::test_gst_source_h264_aac);
      TEST(test_r_pipeline::test_gst_source_h265_aac);
      TEST(test_r_pipeline::test_gst_source_h264_mulaw);
      TEST(test_r_pipeline::test_gst_source_h265_mulaw);
      TEST(test_r_pipeline::test_gst_source_fetch_sdp);
      TEST(test_r_pipeline::test_gst_source_pull_real);
      TEST(test_r_pipeline::test_gst_source_bframes);
      TEST(test_r_pipeline::test_gst_source_fetch_bytes_per_second);
      TEST(test_r_pipeline::test_stream_info_get_info_frames);
      TEST(test_r_pipeline::test_stream_info_make_extradata);
      TEST(test_r_pipeline::test_stream_info_parse_h264_sps);
      TEST(test_r_pipeline::test_stream_info_parse_h265_sps);
    RTF_FIXTURE_END();

    virtual ~test_r_pipeline() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_gst_source_h264_aac();
    void test_gst_source_h265_aac();
    void test_gst_source_h264_mulaw();
    void test_gst_source_h265_mulaw();
    void test_gst_source_fetch_sdp();
    void test_gst_source_pull_real();
    void test_gst_source_bframes();
    void test_gst_source_fetch_bytes_per_second();
    void test_stream_info_get_info_frames();
    void test_stream_info_make_extradata();
    void test_stream_info_parse_h264_sps();
    void test_stream_info_parse_h265_sps();

};
