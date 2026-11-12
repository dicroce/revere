#include "framework.h"

class test_r_network_watch : public test_fixture
{
public:
    RTF_FIXTURE(test_r_network_watch);
      TEST(test_r_network_watch::test_the_first_signature_is_not_a_change);
      TEST(test_r_network_watch::test_a_change_counts_once_it_has_held);
      TEST(test_r_network_watch::test_a_flap_is_not_a_change);
      TEST(test_r_network_watch::test_losing_and_regaining_the_network_are_changes);
      TEST(test_r_network_watch::test_polls_between_checks_do_not_look_at_the_network);
      TEST(test_r_network_watch::test_a_change_drops_every_stream_once);
      TEST(test_r_network_watch::test_vpn_and_vm_links_are_not_the_cameras_network);
    RTF_FIXTURE_END();

    virtual ~test_r_network_watch() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_the_first_signature_is_not_a_change();
    void test_a_change_counts_once_it_has_held();
    void test_a_flap_is_not_a_change();
    void test_losing_and_regaining_the_network_are_changes();
    void test_polls_between_checks_do_not_look_at_the_network();
    void test_a_change_drops_every_stream_once();
    void test_vpn_and_vm_links_are_not_the_cameras_network();
};
