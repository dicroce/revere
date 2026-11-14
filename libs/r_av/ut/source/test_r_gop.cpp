#include "test_r_gop.h"
#include "r_av/r_gop.h"

using namespace r_av;

REGISTER_TEST_FIXTURE(test_r_gop);

void test_r_gop::test_one_second_is_one_frame_rate_of_frames()
{
    RTF_ASSERT(r_gop_frames(AVRational{15, 1}, 1) == 15);
    RTF_ASSERT(r_gop_frames(AVRational{30, 1}, 1) == 30);
}

void test_r_gop::test_two_seconds_is_twice_that()
{
    RTF_ASSERT(r_gop_frames(AVRational{15, 1}, 2) == 30);
    RTF_ASSERT(r_gop_frames(AVRational{30, 1}, 2) == 60);
}

void test_r_gop::test_a_fractional_rate_rounds_up_to_whole_frames()
{
    RTF_ASSERT(r_gop_frames(AVRational{30000, 1001}, 1) == 30);
    RTF_ASSERT(r_gop_frames(AVRational{30000, 1001}, 2) == 60);
}

void test_r_gop::test_a_bad_rate_or_zero_seconds_gives_one_frame()
{
    RTF_ASSERT(r_gop_frames(AVRational{0, 1}, 2) == 1);
    RTF_ASSERT(r_gop_frames(AVRational{15, 0}, 2) == 1);
    RTF_ASSERT(r_gop_frames(AVRational{15, 1}, 0) == 1);
}
