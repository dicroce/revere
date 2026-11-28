#include "framework.h"

class test_plugin_status_line : public test_fixture
{
public:
    RTF_FIXTURE(test_plugin_status_line);
      TEST(test_plugin_status_line::test_action_line);
      TEST(test_plugin_status_line::test_action_needs_a_label);
      TEST(test_plugin_status_line::test_plain_lines_unchanged);
      TEST(test_plugin_status_line::test_button_runs_action);
      TEST(test_plugin_status_line::test_confirm_asks_first);
      TEST(test_plugin_status_line::test_question_dropped_with_its_line);
      TEST(test_plugin_status_line::test_choice_line);
      TEST(test_plugin_status_line::test_choice_that_cannot_be_drawn);
      TEST(test_plugin_status_line::test_choice_pick_runs_action);
      TEST(test_plugin_status_line::test_choice_names_wrap);
    RTF_FIXTURE_END();

    virtual ~test_plugin_status_line() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_action_line();
    void test_action_needs_a_label();
    void test_plain_lines_unchanged();
    void test_button_runs_action();
    void test_confirm_asks_first();
    void test_question_dropped_with_its_line();
    void test_choice_line();
    void test_choice_that_cannot_be_drawn();
    void test_choice_pick_runs_action();
    void test_choice_names_wrap();
};
