#include "framework.h"

class test_r_web_password : public test_fixture
{
public:
    RTF_FIXTURE(test_r_web_password);
      TEST(test_r_web_password::test_auth_status_normal_install);
      TEST(test_r_web_password::test_auth_status_appliance);
      TEST(test_r_web_password::test_normal_bootstrap_from_loopback_only);
      TEST(test_r_web_password::test_normal_change);
      TEST(test_r_web_password::test_appliance_bootstrap_needs_local_token);
      TEST(test_r_web_password::test_appliance_change_needs_local_token);
    RTF_FIXTURE_END();

    virtual ~test_r_web_password() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_auth_status_normal_install();
    void test_auth_status_appliance();
    void test_normal_bootstrap_from_loopback_only();
    void test_normal_change();
    void test_appliance_bootstrap_needs_local_token();
    void test_appliance_change_needs_local_token();
};
