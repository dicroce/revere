#include "framework.h"

class test_configure_state : public test_fixture
{
public:
    RTF_FIXTURE(test_configure_state);
      TEST(test_configure_state::test_no_file);
      TEST(test_configure_state::test_round_trip);
      TEST(test_configure_state::test_truncated_file_kept);
      TEST(test_configure_state::test_bad_answers_kept);
      TEST(test_configure_state::test_save_leaves_no_temp);
    RTF_FIXTURE_END();

    virtual ~test_configure_state() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_no_file();
    void test_round_trip();
    void test_truncated_file_kept();
    void test_bad_answers_kept();
    void test_save_leaves_no_temp();

private:
    std::string _top_dir;
};
