#include "framework.h"

class test_r_opus_transcoder : public test_fixture
{
public:
    RTF_FIXTURE(test_r_opus_transcoder);
      TEST(test_r_opus_transcoder::test_mulaw_becomes_opus);
      TEST(test_r_opus_transcoder::test_packet_lengths);
      TEST(test_r_opus_transcoder::test_aac_becomes_opus);
      TEST(test_r_opus_transcoder::test_why_not);
      TEST(test_r_opus_transcoder::test_rtp_packet_payload);
    RTF_FIXTURE_END();

    virtual ~test_r_opus_transcoder() throw() {}

    virtual void setup();
    virtual void teardown() {}

    void test_mulaw_becomes_opus();
    void test_packet_lengths();
    void test_aac_becomes_opus();
    void test_why_not();
    void test_rtp_packet_payload();
};
