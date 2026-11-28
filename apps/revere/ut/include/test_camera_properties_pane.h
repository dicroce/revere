#include "framework.h"

class test_camera_properties_pane : public test_fixture
{
public:
    RTF_FIXTURE(test_camera_properties_pane);
      TEST(test_camera_properties_pane::test_the_smaller_stream_can_be_chosen_only_when_it_works);
      TEST(test_camera_properties_pane::test_a_choice_already_made_stays_choosable_and_can_go_back);
    RTF_FIXTURE_END();

    virtual ~test_camera_properties_pane() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_the_smaller_stream_can_be_chosen_only_when_it_works();
    void test_a_choice_already_made_stays_choosable_and_can_go_back();
};
