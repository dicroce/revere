#include "framework.h"

class test_r_frame_tap : public test_fixture
{
public:
    RTF_FIXTURE(test_r_frame_tap);
      TEST(test_r_frame_tap::test_decodes_every_frame_and_keeps_the_newest);
      TEST(test_r_frame_tap::test_picture_is_scaled_to_the_long_edge);
      TEST(test_r_frame_tap::test_waits_for_a_key_frame);
      TEST(test_r_frame_tap::test_a_full_queue_starts_again_at_a_key_frame);
      TEST(test_r_frame_tap::test_a_picture_is_encoded_once);
      TEST(test_r_frame_tap::test_taps_start_on_ask_and_only_take_their_camera);
      TEST(test_r_frame_tap::test_at_most_max_taps);
      TEST(test_r_frame_tap::test_an_idle_tap_stops);
    RTF_FIXTURE_END();

    virtual ~test_r_frame_tap() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_decodes_every_frame_and_keeps_the_newest();
    void test_picture_is_scaled_to_the_long_edge();
    void test_waits_for_a_key_frame();
    void test_a_full_queue_starts_again_at_a_key_frame();
    void test_a_picture_is_encoded_once();
    void test_taps_start_on_ask_and_only_take_their_camera();
    void test_at_most_max_taps();
    void test_an_idle_tap_stops();
};
