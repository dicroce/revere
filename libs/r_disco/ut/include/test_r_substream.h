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
};
