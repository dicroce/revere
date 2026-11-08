#include "framework.h"

class test_r_failure_reason : public test_fixture
{
public:
    RTF_FIXTURE(test_r_failure_reason);
      TEST(test_r_failure_reason::test_first_line_only);
      TEST(test_r_failure_reason::test_source_position_dropped);
      TEST(test_r_failure_reason::test_other_paths_cut_to_last_part);
      TEST(test_r_failure_reason::test_url_credentials_removed);
      TEST(test_r_failure_reason::test_url_keeps_its_host_and_port);
      TEST(test_r_failure_reason::test_path_with_spaces);
      TEST(test_r_failure_reason::test_text_from_a_caller_is_bounded_and_clean);
    RTF_FIXTURE_END();

    virtual ~test_r_failure_reason() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_first_line_only();
    void test_source_position_dropped();
    void test_other_paths_cut_to_last_part();
    void test_url_credentials_removed();
    void test_url_keeps_its_host_and_port();
    void test_path_with_spaces();
    void test_text_from_a_caller_is_bounded_and_clean();
};
