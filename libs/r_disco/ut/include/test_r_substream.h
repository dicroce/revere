#include "framework.h"

class test_r_substream : public test_fixture
{
public:
    RTF_FIXTURE(test_r_substream);
      TEST(test_r_substream::test_picks_the_best_candidate_that_passes);
      TEST(test_r_substream::test_tries_the_next_when_the_best_fails);
      TEST(test_r_substream::test_none_gives_the_best_candidates_reason);
      TEST(test_r_substream::test_no_candidates_opens_nothing);
      TEST(test_r_substream::test_no_stream_address);
      TEST(test_r_substream::test_probes_at_most_the_limit);
      TEST(test_r_substream::test_squeezed_picture_is_kept);
      TEST(test_r_substream::test_set_records_the_pick);
      TEST(test_r_substream::test_clear_goes_back_to_main);
      TEST(test_r_substream::test_main_picture_size);
      TEST(test_r_substream::test_copy_takes_only_the_substream);
      TEST(test_r_substream::test_follow_keeps_the_same_stream);
      TEST(test_r_substream::test_follow_drops_a_resized_stream);
      TEST(test_r_substream::test_follow_without_an_sps_is_not_a_change);
      TEST(test_r_substream::test_main_profile_index);
      TEST(test_r_substream::test_stream_titles);
      TEST(test_r_substream::test_none_reason);
      TEST(test_r_substream::test_reason_hints);
    RTF_FIXTURE_END();

    virtual ~test_r_substream() throw() {}
    virtual void setup() {}
    virtual void teardown() {}

    void test_picks_the_best_candidate_that_passes();
    void test_tries_the_next_when_the_best_fails();
    void test_none_gives_the_best_candidates_reason();
    void test_no_candidates_opens_nothing();
    void test_no_stream_address();
    void test_probes_at_most_the_limit();
    void test_squeezed_picture_is_kept();
    void test_set_records_the_pick();
    void test_clear_goes_back_to_main();
    void test_main_picture_size();
    void test_copy_takes_only_the_substream();
    void test_follow_keeps_the_same_stream();
    void test_follow_drops_a_resized_stream();
    void test_follow_without_an_sps_is_not_a_change();
    void test_main_profile_index();
    void test_stream_titles();
    void test_none_reason();
    void test_reason_hints();
};
