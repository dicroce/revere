
#include "framework.h"

class test_r_disco : public test_fixture
{
public:
    RTF_FIXTURE(test_r_disco);
      TEST(test_r_disco::test_r_disco_r_agent_basics);
      TEST(test_r_disco::test_camera_move_from);
      TEST(test_r_disco::test_decide_camera_move);
      TEST(test_r_disco::test_agent_holds_a_move_and_writes_nothing);
      TEST(test_r_disco::test_agent_follows_when_old_silent_and_mac_matches);
      TEST(test_r_disco::test_agent_holds_when_old_alive_mac_differs_or_unknown);
      TEST(test_r_disco::test_agent_confirm_and_dismiss);
      TEST(test_r_disco::test_agent_backfills_mac_at_stored_address);
      TEST(test_r_disco::test_db_upgrade_from_mac_at_version_8);
      TEST(test_r_disco::test_db_upgrade_from_device_info_at_version_8);
      TEST(test_r_disco::test_db_upgrade_repairs_a_missing_column);
      TEST(test_r_disco::test_db_upgrade_keeps_a_newer_version);
      TEST(test_r_disco::test_db_upgrade_twice);
      TEST(test_r_disco::test_db_upgrade_from_version_7);
    RTF_FIXTURE_END();

    virtual ~test_r_disco() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_r_disco_r_agent_basics();
    void test_camera_move_from();
    void test_decide_camera_move();
    void test_agent_holds_a_move_and_writes_nothing();
    void test_agent_follows_when_old_silent_and_mac_matches();
    void test_agent_holds_when_old_alive_mac_differs_or_unknown();
    void test_agent_confirm_and_dismiss();
    void test_agent_backfills_mac_at_stored_address();
    void test_db_upgrade_from_mac_at_version_8();
    void test_db_upgrade_from_device_info_at_version_8();
    void test_db_upgrade_repairs_a_missing_column();
    void test_db_upgrade_keeps_a_newer_version();
    void test_db_upgrade_twice();
    void test_db_upgrade_from_version_7();
};
