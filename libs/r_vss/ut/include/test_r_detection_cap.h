#include "framework.h"

class test_r_detection_cap : public test_fixture
{
public:
    RTF_FIXTURE(test_r_detection_cap);
      TEST(test_r_detection_cap::test_under_the_cap_keeps_all);
      TEST(test_r_detection_cap::test_long_busy_event);
      TEST(test_r_detection_cap::test_whole_frames_within_budget);
      TEST(test_r_detection_cap::test_smaller_frames_fill_past_a_big_one);
      TEST(test_r_detection_cap::test_best_frames_kept_whatever_their_size);
    RTF_FIXTURE_END();

    virtual ~test_r_detection_cap() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_under_the_cap_keeps_all();
    void test_long_busy_event();
    void test_whole_frames_within_budget();
    void test_smaller_frames_fill_past_a_big_one();
    void test_best_frames_kept_whatever_their_size();
};
