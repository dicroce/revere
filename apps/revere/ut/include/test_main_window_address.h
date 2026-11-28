#include "framework.h"

class test_main_window_address : public test_fixture
{
public:
    RTF_FIXTURE(test_main_window_address);
      TEST(test_main_window_address::test_a_held_camera_has_its_my_camera_button);
      TEST(test_main_window_address::test_a_camera_that_is_not_held_has_none);
    RTF_FIXTURE_END();

    virtual ~test_main_window_address() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_a_held_camera_has_its_my_camera_button();
    void test_a_camera_that_is_not_held_has_none();
};
