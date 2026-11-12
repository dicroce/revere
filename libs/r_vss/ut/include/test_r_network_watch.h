#include "framework.h"

class test_r_network_watch : public test_fixture
{
public:
    RTF_FIXTURE(test_r_network_watch);
      TEST(test_r_network_watch::test_the_first_signature_is_not_a_change);
      TEST(test_r_network_watch::test_a_change_counts_once_it_holds);
      TEST(test_r_network_watch::test_a_flap_is_not_a_change);
      TEST(test_r_network_watch::test_losing_and_regaining_the_network_are_changes);
    RTF_FIXTURE_END();

    virtual ~test_r_network_watch() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_the_first_signature_is_not_a_change();
    void test_a_change_counts_once_it_holds();
    void test_a_flap_is_not_a_change();
    void test_losing_and_regaining_the_network_are_changes();
};
