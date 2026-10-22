#include "framework.h"

class test_text_selection : public test_fixture
{
public:
    RTF_FIXTURE(test_text_selection);
      TEST(test_text_selection::test_within_one_line);
      TEST(test_text_selection::test_across_lines);
      TEST(test_text_selection::test_dragged_backwards);
      TEST(test_text_selection::test_line_span);
      TEST(test_text_selection::test_lines_scrolled_out);
      TEST(test_text_selection::test_empty);
      TEST(test_text_selection::test_all_text);
      TEST(test_text_selection::test_col_at_x);
      TEST(test_text_selection::test_col_at_x_utf8);
    RTF_FIXTURE_END();

    virtual ~test_text_selection() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_within_one_line();
    void test_across_lines();
    void test_dragged_backwards();
    void test_line_span();
    void test_lines_scrolled_out();
    void test_empty();
    void test_all_text();
    void test_col_at_x();
    void test_col_at_x_utf8();
};
