#include "framework.h"

class test_x11_errors : public test_fixture
{
public:
    RTF_FIXTURE(test_x11_errors);
      TEST(test_x11_errors::test_core_errors_on_randr_requests_are_not);
      TEST(test_x11_errors::test_randr_error_codes_are_randr);
      TEST(test_x11_errors::test_other_errors_are_not);
      TEST(test_x11_errors::test_no_randr_on_the_server);
    RTF_FIXTURE_END();

    virtual ~test_x11_errors() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_core_errors_on_randr_requests_are_not();
    void test_randr_error_codes_are_randr();
    void test_other_errors_are_not();
    void test_no_randr_on_the_server();
};
