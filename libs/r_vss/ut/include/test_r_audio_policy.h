#include "framework.h"

class test_r_audio_policy : public test_fixture
{
public:
    RTF_FIXTURE(test_r_audio_policy);
      TEST(test_r_audio_policy::test_parse);
      TEST(test_r_audio_policy::test_records_audio);
      TEST(test_r_audio_policy::test_file);
      TEST(test_r_audio_policy::test_restarts);
      TEST(test_r_audio_policy::test_sdp_without_audio);
      TEST(test_r_audio_policy::test_who_may_set_it);
    RTF_FIXTURE_END();

    virtual ~test_r_audio_policy() throw() {}

    virtual void setup() {}
    virtual void teardown();

    void test_parse();
    void test_records_audio();
    void test_file();
    void test_restarts();
    void test_sdp_without_audio();
    void test_who_may_set_it();
};
