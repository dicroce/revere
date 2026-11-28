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
      TEST(test_plugin_status_line::test_choice_ring_is_for_the_selected_row);
      TEST(test_plugin_status_line::test_list_line);
      TEST(test_plugin_status_line::test_list_row_opens_its_detail);
      TEST(test_plugin_status_line::test_list_back_and_vanished_row);
      TEST(test_plugin_status_line::test_a_row_that_went_away_does_not_come_back_open);
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
    void test_choice_ring_is_for_the_selected_row();
    void test_list_line();
    void test_list_row_opens_its_detail();
    void test_list_back_and_vanished_row();
    void test_a_row_that_went_away_does_not_come_back_open();
};
