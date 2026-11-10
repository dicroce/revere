
#include "framework.h"

class test_r_transcoder_repro : public test_fixture
{
public:
    RTF_FIXTURE(test_r_transcoder_repro);
      TEST(test_r_transcoder_repro::test_4k_transcode);
      TEST(test_r_transcoder_repro::test_audio_written_as_it_comes);
      TEST(test_r_transcoder_repro::test_no_audio_without_it);
      TEST(test_r_transcoder_repro::test_quiet_audio_does_not_hold_video);
      TEST(test_r_transcoder_repro::test_g711_as_aac_over_srt);
      TEST(test_r_transcoder_repro::test_writes_return_once_the_output_fails);
    RTF_FIXTURE_END();

    virtual ~test_r_transcoder_repro() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_4k_transcode();
    void test_audio_written_as_it_comes();
    void test_no_audio_without_it();
    void test_quiet_audio_does_not_hold_video();
    void test_g711_as_aac_over_srt();
    void test_writes_return_once_the_output_fails();
};
