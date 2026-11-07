#include "framework.h"

class test_r_recap : public test_fixture
{
public:
    RTF_FIXTURE(test_r_recap);
      TEST(test_r_recap::test_pick_evenly);
      TEST(test_r_recap::test_plan);
      TEST(test_r_recap::test_pad);
      TEST(test_r_recap::test_recap_from_key_frames);
      TEST(test_r_recap::test_recap_decoding_every_frame);
      TEST(test_r_recap::test_recap_across_a_gap);
      TEST(test_r_recap::test_frames_decoded_show_their_times);
      TEST(test_r_recap::test_key_frames_show_their_times);
      TEST(test_r_recap::test_no_recording);
    RTF_FIXTURE_END();

    virtual ~test_r_recap() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_pick_evenly();
    void test_plan();
    void test_pad();
    void test_recap_from_key_frames();
    void test_recap_decoding_every_frame();
    void test_recap_across_a_gap();
    void test_frames_decoded_show_their_times();
    void test_key_frames_show_their_times();
    void test_no_recording();
};
