#include "framework.h"

class test_camera_properties_state : public test_fixture
{
public:
    RTF_FIXTURE(test_camera_properties_state);
      TEST(test_camera_properties_state::test_nothing_changed_is_not_dirty);
      TEST(test_camera_properties_state::test_each_setting_makes_it_dirty);
      TEST(test_camera_properties_state::test_hours_count_only_with_pruning);
      TEST(test_camera_properties_state::test_hours_must_be_a_whole_number_of_hours);
      TEST(test_camera_properties_state::test_begin_starts_over);
      TEST(test_camera_properties_state::test_the_press_source_counts_only_on_a_doorbell);
      TEST(test_camera_properties_state::test_the_press_test_lines);
      TEST(test_camera_properties_state::test_the_press_test_verdict);
      TEST(test_camera_properties_state::test_the_mail_status_lines);
      TEST(test_camera_properties_state::test_a_password_in_groups_of_4);
      TEST(test_camera_properties_state::test_the_mail_setup_rows);
    RTF_FIXTURE_END();

    virtual ~test_camera_properties_state() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_nothing_changed_is_not_dirty();
    void test_each_setting_makes_it_dirty();
    void test_hours_count_only_with_pruning();
    void test_hours_must_be_a_whole_number_of_hours();
    void test_begin_starts_over();
    void test_the_press_source_counts_only_on_a_doorbell();
    void test_the_press_test_lines();
    void test_the_press_test_verdict();
    void test_the_mail_status_lines();
    void test_a_password_in_groups_of_4();
    void test_the_mail_setup_rows();
};
