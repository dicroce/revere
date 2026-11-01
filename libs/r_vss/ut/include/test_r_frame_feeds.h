#include "framework.h"

class test_r_frame_feeds : public test_fixture
{
public:
    RTF_FIXTURE(test_r_frame_feeds);
      TEST(test_r_frame_feeds::test_nothing_subscribed_costs_nothing);
      TEST(test_r_frame_feeds::test_video_starts_at_a_key_frame);
      TEST(test_r_frame_feeds::test_media_chooses_video_audio_or_both);
      TEST(test_r_frame_feeds::test_audio_while_video_waits_for_a_key_frame);
      TEST(test_r_frame_feeds::test_a_full_queue_starts_again_with_a_gap);
      TEST(test_r_frame_feeds::test_a_frame_bigger_than_the_bound);
      TEST(test_r_frame_feeds::test_codec_changes_are_marked);
      TEST(test_r_frame_feeds::test_streams);
      TEST(test_r_frame_feeds::test_unsubscribe_wakes_a_waiting_reader);
      TEST(test_r_frame_feeds::test_closing_a_camera_and_all);
      TEST(test_r_frame_feeds::test_a_wait_times_out);
      TEST(test_r_frame_feeds::test_two_feeds_get_their_own_copies);
    RTF_FIXTURE_END();

    virtual ~test_r_frame_feeds() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_nothing_subscribed_costs_nothing();
    void test_video_starts_at_a_key_frame();
    void test_media_chooses_video_audio_or_both();
    void test_audio_while_video_waits_for_a_key_frame();
    void test_a_full_queue_starts_again_with_a_gap();
    void test_a_frame_bigger_than_the_bound();
    void test_codec_changes_are_marked();
    void test_streams();
    void test_unsubscribe_wakes_a_waiting_reader();
    void test_closing_a_camera_and_all();
    void test_a_wait_times_out();
    void test_two_feeds_get_their_own_copies();
};
