
#include "framework.h"

class test_r_onvif : public test_fixture
{
public:
    RTF_FIXTURE(test_r_onvif);
      TEST(test_r_onvif::test_r_onvif_session_basic);
      TEST(test_r_onvif::test_r_onvif_event_capabilities);
      TEST(test_r_onvif::test_r_onvif_raw_datetime);
      TEST(test_r_onvif::test_r_onvif_raw_capabilities);
    RTF_FIXTURE_END();

    virtual ~test_r_onvif() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_r_onvif_session_basic();
    void test_r_onvif_event_capabilities();
    void test_r_onvif_raw_datetime();
    void test_r_onvif_raw_capabilities();
};
