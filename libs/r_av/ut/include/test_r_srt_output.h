#include "framework.h"

class test_r_srt_output : public test_fixture
{
public:
    RTF_FIXTURE(test_r_srt_output);
      TEST(test_r_srt_output::test_sends_to_a_listener_and_reports_stats);
      TEST(test_r_srt_output::test_a_wrong_passphrase_does_not_connect);
      TEST(test_r_srt_output::test_a_closed_output_fails_its_writes);
      TEST(test_r_srt_output::test_a_muxer_sends_mpegts_through_libsrt_and_reads_stats);
      TEST(test_r_srt_output::test_a_muxer_that_is_not_srt_has_no_stats);
    RTF_FIXTURE_END();

    virtual ~test_r_srt_output() throw() {}
    virtual void setup() {}
    virtual void teardown() {}

    void test_sends_to_a_listener_and_reports_stats();
    void test_a_wrong_passphrase_does_not_connect();
    void test_a_closed_output_fails_its_writes();
    void test_a_muxer_sends_mpegts_through_libsrt_and_reads_stats();
    void test_a_muxer_that_is_not_srt_has_no_stats();
};
