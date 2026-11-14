#include "framework.h"

class test_r_gop : public test_fixture
{
public:
    RTF_FIXTURE(test_r_gop);
      TEST(test_r_gop::test_one_second_is_one_frame_rate_of_frames);
      TEST(test_r_gop::test_two_seconds_is_twice_that);
      TEST(test_r_gop::test_a_fractional_rate_rounds_up_to_whole_frames);
      TEST(test_r_gop::test_a_bad_rate_or_zero_seconds_gives_one_frame);
    RTF_FIXTURE_END();

    virtual ~test_r_gop() throw() {}
    virtual void setup() {}
    virtual void teardown() {}

    void test_one_second_is_one_frame_rate_of_frames();
    void test_two_seconds_is_twice_that();
    void test_a_fractional_rate_rounds_up_to_whole_frames();
    void test_a_bad_rate_or_zero_seconds_gives_one_frame();
};
