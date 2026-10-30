#include "framework.h"

class test_tray_status : public test_fixture
{
public:
    RTF_FIXTURE(test_tray_status);
      TEST(test_tray_status::test_unreachable);
      TEST(test_tray_status::test_bad_json);
      TEST(test_tray_status::test_no_assigned_cameras);
      TEST(test_tray_status::test_one_recording);
      TEST(test_tray_status::test_all_recording);
      TEST(test_tray_status::test_some_failed);
      TEST(test_tray_status::test_connecting);
      TEST(test_tray_status::test_many_failed_names);
    RTF_FIXTURE_END();

    virtual ~test_tray_status() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_unreachable();
    void test_bad_json();
    void test_no_assigned_cameras();
    void test_one_recording();
    void test_all_recording();
    void test_some_failed();
    void test_connecting();
    void test_many_failed_names();
};
