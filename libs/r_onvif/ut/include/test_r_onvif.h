
#include "framework.h"

class test_r_onvif : public test_fixture
{
public:
    RTF_FIXTURE(test_r_onvif);
      TEST(test_r_onvif::test_r_onvif_session_basic);
    RTF_FIXTURE_END();

    virtual ~test_r_onvif() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_r_onvif_session_basic();
};
