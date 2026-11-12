#include "framework.h"

class test_r_live_fanout : public test_fixture
{
public:
    RTF_FIXTURE(test_r_live_fanout);
      TEST(test_r_live_fanout::test_the_cache_starts_at_a_key_frame);
      TEST(test_r_live_fanout::test_a_new_viewer_gets_the_cached_gop_as_a_burst);
      TEST(test_r_live_fanout::test_a_viewer_waits_for_a_key_frame_without_a_cache);
      TEST(test_r_live_fanout::test_a_viewer_gets_each_frame_after_its_first_key_frame);
      TEST(test_r_live_fanout::test_the_cache_is_capped);
      TEST(test_r_live_fanout::test_two_viewers_are_independent);
    RTF_FIXTURE_END();

    virtual ~test_r_live_fanout() throw() {}
    virtual void setup() {}
    virtual void teardown() {}

    void test_the_cache_starts_at_a_key_frame();
    void test_a_new_viewer_gets_the_cached_gop_as_a_burst();
    void test_a_viewer_waits_for_a_key_frame_without_a_cache();
    void test_a_viewer_gets_each_frame_after_its_first_key_frame();
    void test_the_cache_is_capped();
    void test_two_viewers_are_independent();
};
