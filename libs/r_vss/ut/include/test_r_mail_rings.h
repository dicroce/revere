#include "framework.h"

// r_mail_rings: doorbell rings by email, over a real loopback SMTP session with STARTTLS.
class test_r_mail_rings : public test_fixture
{
public:
    RTF_FIXTURE(test_r_mail_rings);
      TEST(test_r_mail_rings::test_reading_a_ring_mail);
      TEST(test_r_mail_rings::test_the_press_time);
      TEST(test_r_mail_rings::test_a_ring_over_starttls_with_auth_plain);
      TEST(test_r_mail_rings::test_auth_login_and_a_motion_mail);
      TEST(test_r_mail_rings::test_nothing_before_starttls);
      TEST(test_r_mail_rings::test_a_wrong_login);
      TEST(test_r_mail_rings::test_a_stranger_is_refused);
      TEST(test_r_mail_rings::test_a_mail_too_big);
      TEST(test_r_mail_rings::test_too_many_mails_a_minute);
      TEST(test_r_mail_rings::test_a_slow_client_is_let_go);
      TEST(test_r_mail_rings::test_it_never_binds_every_address);
      TEST(test_r_mail_rings::test_nothing_may_follow_starttls_in_the_clear);
      TEST(test_r_mail_rings::test_too_many_connections_at_once);
    RTF_FIXTURE_END();

    virtual ~test_r_mail_rings() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_reading_a_ring_mail();
    void test_the_press_time();
    void test_a_ring_over_starttls_with_auth_plain();
    void test_auth_login_and_a_motion_mail();
    void test_nothing_before_starttls();
    void test_a_wrong_login();
    void test_a_stranger_is_refused();
    void test_a_mail_too_big();
    void test_too_many_mails_a_minute();
    void test_a_slow_client_is_let_go();
    void test_it_never_binds_every_address();
    void test_nothing_may_follow_starttls_in_the_clear();
    void test_too_many_connections_at_once();
};
