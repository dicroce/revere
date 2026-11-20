#include "framework.h"

class test_r_client_errors : public test_fixture
{
public:
    RTF_FIXTURE(test_r_client_errors);
      TEST(test_r_client_errors::test_handlers_pass_client_errors_on);
      TEST(test_r_client_errors::test_a_bad_time_is_a_bad_argument);
      TEST(test_r_client_errors::test_queries_say_not_found);
      TEST(test_r_client_errors::test_a_full_disk_is_507);
      TEST(test_r_client_errors::test_server_answers_client_errors_with_json);
      TEST(test_r_client_errors::test_client_errors_log_one_line_each_rate_limited);
    RTF_FIXTURE_END();

    virtual ~test_r_client_errors() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_handlers_pass_client_errors_on();
    void test_a_bad_time_is_a_bad_argument();
    void test_queries_say_not_found();
    void test_a_full_disk_is_507();
    void test_server_answers_client_errors_with_json();
    void test_client_errors_log_one_line_each_rate_limited();
};
