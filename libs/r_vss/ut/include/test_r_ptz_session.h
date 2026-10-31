
#include "framework.h"

class test_r_ptz_session : public test_fixture
{
public:
    RTF_FIXTURE(test_r_ptz_session);
      TEST(test_r_ptz_session::test_session_kept_between_commands);
      TEST(test_r_ptz_session::test_named_profile_is_used);
      TEST(test_r_ptz_session::test_no_ptz_service);
      TEST(test_r_ptz_session::test_stale_session_opened_again_once);
      TEST(test_r_ptz_session::test_new_session_failure_not_retried);
      TEST(test_r_ptz_session::test_new_credentials_new_session);
      TEST(test_r_ptz_session::test_tapo_port_found);
      TEST(test_r_ptz_session::test_cameras_have_own_sessions);
      TEST(test_r_ptz_session::test_open_failure_is_camera_error);
      TEST(test_r_ptz_session::test_list_presets_reports_axes);
      TEST(test_r_ptz_session::test_axes_unknown_are_null);
    RTF_FIXTURE_END();

    virtual ~test_r_ptz_session() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_session_kept_between_commands();
    void test_named_profile_is_used();
    void test_no_ptz_service();
    void test_stale_session_opened_again_once();
    void test_new_session_failure_not_retried();
    void test_new_credentials_new_session();
    void test_tapo_port_found();
    void test_cameras_have_own_sessions();
    void test_open_failure_is_camera_error();
    void test_list_presets_reports_axes();
    void test_axes_unknown_are_null();
};
