
#include "framework.h"

// Offline tests of the stream probe's tally and the substream check.
class test_r_stream_probe : public test_fixture
{
public:
    RTF_FIXTURE(test_r_stream_probe);
      TEST(test_r_stream_probe::test_h264_without_b_frames);
      TEST(test_r_stream_probe::test_h264_b_slices);
      TEST(test_r_stream_probe::test_h265);
      TEST(test_r_stream_probe::test_pts_going_backwards);
      TEST(test_r_stream_probe::test_keyframe_interval_and_fps);
      TEST(test_r_stream_probe::test_sps_aspect_and_reorder);
      TEST(test_r_stream_probe::test_check_substream);
      TEST(test_r_stream_probe::test_anamorphic);
      TEST(test_r_stream_probe::test_probe_stream);
      TEST(test_r_stream_probe::test_probe_stream_b_frames);
      TEST(test_r_stream_probe::test_probe_stream_no_camera);
    RTF_FIXTURE_END();

    virtual ~test_r_stream_probe() throw() {}

    virtual void setup();
    virtual void teardown() {}

    void test_h264_without_b_frames();
    void test_h264_b_slices();
    void test_h265();
    void test_pts_going_backwards();
    void test_keyframe_interval_and_fps();
    void test_sps_aspect_and_reorder();
    void test_check_substream();
    void test_anamorphic();
    void test_probe_stream();
    void test_probe_stream_b_frames();
    void test_probe_stream_no_camera();
};
