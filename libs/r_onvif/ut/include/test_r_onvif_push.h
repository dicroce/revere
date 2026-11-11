#include "framework.h"

// ONVIF push events: the Notify parse, the request head, the listener's checks, and Subscribe/Renew/Unsubscribe against a
// fake camera. All on loopback.
class test_r_onvif_push : public test_fixture
{
public:
    RTF_FIXTURE(test_r_onvif_push);
      TEST(test_r_onvif_push::test_push_head);
      TEST(test_r_onvif_push::test_ids_and_addresses);
      TEST(test_r_onvif_push::test_notify_parse);
      TEST(test_r_onvif_push::test_notify_limits);
      TEST(test_r_onvif_push::test_pull_point_support);
      TEST(test_r_onvif_push::test_listener_delivers);
      TEST(test_r_onvif_push::test_listener_turns_away);
      TEST(test_r_onvif_push::test_listener_bare_path);
      TEST(test_r_onvif_push::test_granted_seconds);
      TEST(test_r_onvif_push::test_remove_waits_for_a_running_handler);
      TEST(test_r_onvif_push::test_listener_shares_and_ports);
      TEST(test_r_onvif_push::test_subscribe_renew_unsubscribe);
    RTF_FIXTURE_END();

    virtual ~test_r_onvif_push() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_push_head();
    void test_ids_and_addresses();
    void test_notify_parse();
    void test_notify_limits();
    void test_pull_point_support();
    void test_listener_delivers();
    void test_listener_turns_away();
    void test_listener_bare_path();
    void test_granted_seconds();
    void test_remove_waits_for_a_running_handler();
    void test_listener_shares_and_ports();
    void test_subscribe_renew_unsubscribe();
};
