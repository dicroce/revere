#include "framework.h"

class test_main_window_size : public test_fixture
{
public:
    RTF_FIXTURE(test_main_window_size);
      TEST(test_main_window_size::test_no_room_draws_without_asserting);
      TEST(test_main_window_size::test_log_pane_taller_than_the_window);
    RTF_FIXTURE_END();

    virtual ~test_main_window_size() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_no_room_draws_without_asserting();
    void test_log_pane_taller_than_the_window();
};
