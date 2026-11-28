#include "framework.h"

class test_r_appliance : public test_fixture
{
public:
    RTF_FIXTURE(test_r_appliance);
      TEST(test_r_appliance::test_read_flag_missing_file);
      TEST(test_r_appliance::test_read_flag_true);
      TEST(test_r_appliance::test_read_flag_false_or_bad);
      TEST(test_r_appliance::test_read_flag_never_logs_secret);
      TEST(test_r_appliance::test_auth_status_fields);
      TEST(test_r_appliance::test_set_password_bootstrap);
      TEST(test_r_appliance::test_set_password_normal_install);
      TEST(test_r_appliance::test_set_password_appliance);
      TEST(test_r_appliance::test_login_password_appliance);
      TEST(test_r_appliance::test_login_password_normal_install);
    RTF_FIXTURE_END();

    virtual ~test_r_appliance() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_read_flag_missing_file();
    void test_read_flag_true();
    void test_read_flag_false_or_bad();
    void test_read_flag_never_logs_secret();
    void test_auth_status_fields();
    void test_set_password_bootstrap();
    void test_set_password_normal_install();
    void test_set_password_appliance();
    void test_login_password_appliance();
    void test_login_password_normal_install();
};
