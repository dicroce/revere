
#include "framework.h"

class test_r_transcoder_repro : public test_fixture
{
public:
    RTF_FIXTURE(test_r_transcoder_repro);
      TEST(test_r_transcoder_repro::test_4k_transcode);
    RTF_FIXTURE_END();

    virtual ~test_r_transcoder_repro() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_4k_transcode();
};
