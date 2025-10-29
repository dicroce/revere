
#include "framework.h"

class test_r_http : public test_fixture
{
public:
    RTF_FIXTURE(test_r_http);
      TEST(test_r_http::test_client_request_default_ctor);
      TEST(test_r_http::test_client_request_copy_ctor);
      TEST(test_r_http::test_client_request_assignment_op);
      TEST(test_r_http::test_client_request_write_request);
      TEST(test_r_http::test_client_response_default_constructor);
      TEST(test_r_http::test_client_response_receive);
      TEST(test_r_http::test_client_response_streaming);
      TEST(test_r_http::test_client_response_100_continue);
      TEST(test_r_http::test_client_response_multi_part);
    RTF_FIXTURE_END();

    virtual ~test_r_http() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_client_request_default_ctor();
    void test_client_request_copy_ctor();
    void test_client_request_assignment_op();
    void test_client_request_write_request();
    void test_client_response_default_constructor();
    void test_client_response_receive();
    void test_client_response_streaming();
    void test_client_response_100_continue();
    void test_client_response_multi_part();
};
