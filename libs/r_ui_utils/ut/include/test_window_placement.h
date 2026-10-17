#include "framework.h"

class test_window_placement : public test_fixture
{
public:
    RTF_FIXTURE(test_window_placement);
      TEST(test_window_placement::test_kept_where_it_was);
      TEST(test_window_placement::test_display_gone);
      TEST(test_window_placement::test_partly_off_is_pulled_in);
      TEST(test_window_placement::test_too_big_shrinks);
      TEST(test_window_placement::test_second_display_left_of_main);
      TEST(test_window_placement::test_barely_visible_is_not_kept);
      TEST(test_window_placement::test_title_bar_kept_on_screen);
      TEST(test_window_placement::test_nothing_saved);
    RTF_FIXTURE_END();

    virtual ~test_window_placement() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_kept_where_it_was();
    void test_display_gone();
    void test_partly_off_is_pulled_in();
    void test_too_big_shrinks();
    void test_second_display_left_of_main();
    void test_barely_visible_is_not_kept();
    void test_title_bar_kept_on_screen();
    void test_nothing_saved();
};
