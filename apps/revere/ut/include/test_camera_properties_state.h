#include "framework.h"

class test_camera_properties_state : public test_fixture
{
public:
    RTF_FIXTURE(test_camera_properties_state);
      TEST(test_camera_properties_state::test_nothing_changed_is_not_dirty);
      TEST(test_camera_properties_state::test_each_setting_makes_it_dirty);
      TEST(test_camera_properties_state::test_the_stream_recorded_is_a_setting);
      TEST(test_camera_properties_state::test_what_record_from_says);
      TEST(test_camera_properties_state::test_hours_count_only_with_pruning);
      TEST(test_camera_properties_state::test_hours_must_be_a_whole_number_of_hours);
      TEST(test_camera_properties_state::test_begin_starts_over);
      TEST(test_camera_properties_state::test_the_press_source_counts_only_on_a_doorbell);
      TEST(test_camera_properties_state::test_the_press_test_lines);
      TEST(test_camera_properties_state::test_the_press_test_verdict);
      TEST(test_camera_properties_state::test_the_mail_status_lines);
      TEST(test_camera_properties_state::test_the_password_is_shown_as_it_is);
      TEST(test_camera_properties_state::test_the_mail_setup_rows);
      TEST(test_camera_properties_state::test_the_column_width);
      TEST(test_camera_properties_state::test_a_button_beside_its_text);
      TEST(test_camera_properties_state::test_the_label_column);
      TEST(test_camera_properties_state::test_the_section_tabs_are_the_sections_drawn);
      TEST(test_camera_properties_state::test_the_section_in_view);
      TEST(test_camera_properties_state::test_a_tab_scrolls_to_its_section);
      TEST(test_camera_properties_state::test_a_clicked_tab_stays_marked_until_the_user_scrolls);
    RTF_FIXTURE_END();

    virtual ~test_camera_properties_state() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_nothing_changed_is_not_dirty();
    void test_each_setting_makes_it_dirty();
    void test_the_stream_recorded_is_a_setting();
    void test_what_record_from_says();
    void test_hours_count_only_with_pruning();
    void test_hours_must_be_a_whole_number_of_hours();
    void test_begin_starts_over();
    void test_the_press_source_counts_only_on_a_doorbell();
    void test_the_press_test_lines();
    void test_the_press_test_verdict();
    void test_the_mail_status_lines();
    void test_the_password_is_shown_as_it_is();
    void test_the_mail_setup_rows();
    void test_the_column_width();
    void test_a_button_beside_its_text();
    void test_the_label_column();
    void test_the_section_tabs_are_the_sections_drawn();
    void test_the_section_in_view();
    void test_a_tab_scrolls_to_its_section();
    void test_a_clicked_tab_stays_marked_until_the_user_scrolls();
};
