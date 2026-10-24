#include "framework.h"

class test_r_keep_awake : public test_fixture
{
public:
    RTF_FIXTURE(test_r_keep_awake);
      TEST(test_r_keep_awake::test_wanted);
      TEST(test_r_keep_awake::test_settings_json);
      TEST(test_r_keep_awake::test_power_from_supplies);
      TEST(test_r_keep_awake::test_power_from_status);
      TEST(test_r_keep_awake::test_gap_without_sleep_is_paused);
      TEST(test_r_keep_awake::test_gap_after_sleep_is_asleep);
      TEST(test_r_keep_awake::test_called_off_sleep_is_forgotten);
      TEST(test_r_keep_awake::test_busy_time_is_no_gap);
      TEST(test_r_keep_awake::test_clock_back_is_no_gap);
      TEST(test_r_keep_awake::test_settings_and_events_persist);
      TEST(test_r_keep_awake::test_blocker_holds_and_releases);
      TEST(test_r_keep_awake::test_session_inhibit_survives);
    RTF_FIXTURE_END();

    virtual ~test_r_keep_awake() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_wanted();
    void test_settings_json();
    void test_power_from_supplies();
    void test_power_from_status();
    void test_gap_without_sleep_is_paused();
    void test_gap_after_sleep_is_asleep();
    void test_called_off_sleep_is_forgotten();
    void test_busy_time_is_no_gap();
    void test_clock_back_is_no_gap();
    void test_settings_and_events_persist();
    void test_blocker_holds_and_releases();
    void test_session_inhibit_survives();
};
