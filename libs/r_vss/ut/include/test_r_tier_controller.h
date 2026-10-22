#include "framework.h"

class test_r_tier_controller : public test_fixture
{
public:
    RTF_FIXTURE(test_r_tier_controller);
      TEST(test_r_tier_controller::test_no_subscription_stays_tier_1);
      TEST(test_r_tier_controller::test_matching_motion_moves_to_tier_3);
      TEST(test_r_tier_controller::test_matching_person_moves_to_tier_2);
      TEST(test_r_tier_controller::test_missed_events_stay_tier_1);
      TEST(test_r_tier_controller::test_waits_for_stable_subscription);
      TEST(test_r_tier_controller::test_down_falls_back_and_returns);
      TEST(test_r_tier_controller::test_silence_checks_again);
      TEST(test_r_tier_controller::test_outage_events_not_counted);
      TEST(test_r_tier_controller::test_unconfirmed_events_ignored);
      TEST(test_r_tier_controller::test_tier_3_earns_tier_2);
      TEST(test_r_tier_controller::test_restore);
      TEST(test_r_tier_controller::test_forced_tiers);
    RTF_FIXTURE_END();

    virtual ~test_r_tier_controller() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_no_subscription_stays_tier_1();
    void test_matching_motion_moves_to_tier_3();
    void test_matching_person_moves_to_tier_2();
    void test_missed_events_stay_tier_1();
    void test_waits_for_stable_subscription();
    void test_down_falls_back_and_returns();
    void test_silence_checks_again();
    void test_outage_events_not_counted();
    void test_unconfirmed_events_ignored();
    void test_tier_3_earns_tier_2();
    void test_restore();
    void test_forced_tiers();
};
