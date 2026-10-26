
#include "framework.h"

class test_r_ws_auth : public test_fixture
{
public:
    RTF_FIXTURE(test_r_ws_auth);
      TEST(test_r_ws_auth::test_generate_token);
      TEST(test_r_ws_auth::test_sessions);
      TEST(test_r_ws_auth::test_local_token);
      TEST(test_r_ws_auth::test_throttle_per_address);
      TEST(test_r_ws_auth::test_throttle_failures_expire);
      TEST(test_r_ws_auth::test_throttle_table_bounded);
      TEST(test_r_ws_auth::test_extract_token);
      TEST(test_r_ws_auth::test_cookies);
    RTF_FIXTURE_END();

    virtual ~test_r_ws_auth() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_generate_token();
    void test_sessions();
    void test_local_token();
    void test_throttle_per_address();
    void test_throttle_failures_expire();
    void test_throttle_table_bounded();
    void test_extract_token();
    void test_cookies();
};
