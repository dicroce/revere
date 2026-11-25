#include "framework.h"

class test_r_recording_context : public test_fixture
{
public:
    RTF_FIXTURE(test_r_recording_context);
      TEST(test_r_recording_context::test_source_name_prefix);
      TEST(test_r_recording_context::test_a_feed_starts_from_the_stamped_gop_cache);
      TEST(test_r_recording_context::test_the_video_callback_stamps_the_gop_cache);
      TEST(test_r_recording_context::test_audio_off_stops_audio_before_storage_feeds_and_restream);
      TEST(test_r_recording_context::test_route_audio_sample);
    RTF_FIXTURE_END();

    virtual ~test_r_recording_context() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_source_name_prefix();
    void test_a_feed_starts_from_the_stamped_gop_cache();
    void test_the_video_callback_stamps_the_gop_cache();
    void test_audio_off_stops_audio_before_storage_feeds_and_restream();
    void test_route_audio_sample();
};
