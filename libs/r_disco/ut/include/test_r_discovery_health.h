#include "framework.h"

class test_r_discovery_health : public test_fixture
{
public:
    RTF_FIXTURE(test_r_discovery_health);
      TEST(test_r_discovery_health::test_no_cameras_never_warns);
      TEST(test_r_discovery_health::test_only_direct_answers_warn_after_three_passes);
      TEST(test_r_discovery_health::test_a_multicast_answer_clears_it);
      TEST(test_r_discovery_health::test_silent_passes_say_nothing);
      TEST(test_r_discovery_health::test_some_cameras_ignoring_multicast_is_no_warning);
    RTF_FIXTURE_END();

    virtual ~test_r_discovery_health() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_no_cameras_never_warns();
    void test_only_direct_answers_warn_after_three_passes();
    void test_a_multicast_answer_clears_it();
    void test_silent_passes_say_nothing();
    void test_some_cameras_ignoring_multicast_is_no_warning();
};
