
#include "framework.h"

class test_r_onvif_reboot : public test_fixture
{
public:
    RTF_FIXTURE(test_r_onvif_reboot);
      TEST(test_r_onvif_reboot::test_answers);
      TEST(test_r_onvif_reboot::test_verdicts);
      TEST(test_r_onvif_reboot::test_check_against_a_camera);
      TEST(test_r_onvif_reboot::test_no_credentials_sends_nothing);
      TEST(test_r_onvif_reboot::test_reboot_against_a_camera);
      TEST(test_r_onvif_reboot::test_reboot_without_an_answer_is_sent_once);
    RTF_FIXTURE_END();

    virtual ~test_r_onvif_reboot() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_answers();
    void test_verdicts();
    void test_check_against_a_camera();
    void test_no_credentials_sends_nothing();
    void test_reboot_against_a_camera();
    void test_reboot_without_an_answer_is_sent_once();
};
