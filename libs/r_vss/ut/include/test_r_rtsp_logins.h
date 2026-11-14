#include "framework.h"

class test_r_rtsp_logins : public test_fixture
{
public:
    RTF_FIXTURE(test_r_rtsp_logins);
      TEST(test_r_rtsp_logins::test_login_reaches_mounts_until_removed);
      TEST(test_r_rtsp_logins::test_removal_ends_a_playing_stream);
      TEST(test_r_rtsp_logins::test_routes_need_the_local_token);
      TEST(test_r_rtsp_logins::test_routes_need_loopback);
      TEST(test_r_rtsp_logins::test_list_has_no_passwords);
      TEST(test_r_rtsp_logins::test_cap);
    RTF_FIXTURE_END();

    virtual ~test_r_rtsp_logins() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_login_reaches_mounts_until_removed();
    void test_removal_ends_a_playing_stream();
    void test_routes_need_the_local_token();
    void test_routes_need_loopback();
    void test_list_has_no_passwords();
    void test_cap();
};
