#include "framework.h"

class test_r_restream_opus : public test_fixture
{
public:
    RTF_FIXTURE(test_r_restream_opus);
      TEST(test_r_restream_opus::test_launch_strings);
      TEST(test_r_restream_opus::test_no_opus_without_audio_it_decodes);
      TEST(test_r_restream_opus::test_aac_becomes_opus);
      TEST(test_r_restream_opus::test_mulaw_becomes_opus);
      TEST(test_r_restream_opus::test_mount_describes_opus);
    RTF_FIXTURE_END();

    virtual ~test_r_restream_opus() throw() {}

    virtual void setup();
    virtual void teardown() {}

    void test_launch_strings();
    void test_no_opus_without_audio_it_decodes();
    void test_aac_becomes_opus();
    void test_mulaw_becomes_opus();
    void test_mount_describes_opus();
};
