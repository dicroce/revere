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
};
