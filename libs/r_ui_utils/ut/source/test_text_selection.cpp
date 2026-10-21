#include "test_text_selection.h"
#include "r_ui_utils/text_selection.h"
#include <vector>

using namespace r_ui_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_text_selection);

namespace
{

const deque<string> log_lines = {"alpha one", "bravo two", "charlie three", "delta four"};

// Every byte 10 wide, as a fixed-width font would draw ASCII.
float ten_per_byte(const char* b, const char* e)
{
    return 10.0f * (float)(e - b);
}

}

void test_text_selection::test_within_one_line()
{
    text_selection sel;
    sel.begin({1, 2});
    sel.extend({1, 5});
    RTF_ASSERT_EQUAL(selected_text(sel, log_lines, 0), string("avo"));
}

void test_text_selection::test_across_lines()
{
    // From inside the first line to inside the last: the tail of the first,
    // whole lines between, the head of the last.
    text_selection sel;
    sel.begin({0, 6});
    sel.extend({2, 7});
    RTF_ASSERT_EQUAL(selected_text(sel, log_lines, 0), string("one\nbravo two\ncharlie"));

    // To the start of a line: the line break before it, nothing of it.
    sel.select({0, 6}, {1, 0});
    RTF_ASSERT_EQUAL(selected_text(sel, log_lines, 0), string("one\n"));
}

void test_text_selection::test_dragged_backwards()
{
    text_selection sel;
    sel.begin({2, 7});
    sel.extend({0, 6});
    RTF_ASSERT(sel.start() == (text_pos{0, 6}));
    RTF_ASSERT(sel.end() == (text_pos{2, 7}));
    RTF_ASSERT_EQUAL(selected_text(sel, log_lines, 0), string("one\nbravo two\ncharlie"));
}

void test_text_selection::test_line_span()
{
    text_selection sel;
    sel.select({1, 3}, {3, 2});
    size_t from = 99, to = 99;
    bool eol = false;

    RTF_ASSERT(!sel.line_span(0, 9, from, to, eol));
    RTF_ASSERT(sel.line_span(1, 9, from, to, eol));
    RTF_ASSERT(from == 3 && to == 9 && eol);
    RTF_ASSERT(sel.line_span(2, 13, from, to, eol));
    RTF_ASSERT(from == 0 && to == 13 && eol);
    RTF_ASSERT(sel.line_span(3, 10, from, to, eol));
    RTF_ASSERT(from == 0 && to == 2 && !eol);
    RTF_ASSERT(!sel.line_span(4, 10, from, to, eol));

    // Columns past a line's end (the line is shorter than where the mouse
    // was) are clamped to it.
    sel.select({0, 50}, {0, 80});
    RTF_ASSERT(!sel.line_span(0, 9, from, to, eol));
    sel.select({0, 4}, {0, 80});
    RTF_ASSERT(sel.line_span(0, 9, from, to, eol));
    RTF_ASSERT(from == 4 && to == 9 && !eol);
    // A drag that starts right of a short line's end and goes on down: only
    // that line's break is selected from it.
    sel.select({0, 50}, {1, 3});
    RTF_ASSERT(sel.line_span(0, 9, from, to, eol));
    RTF_ASSERT(from == 9 && to == 9 && eol);
    RTF_ASSERT_EQUAL(selected_text(sel, log_lines, 0), string("\nbra"));

    // An empty line in the middle of a selection is still selected (its
    // line break).
    sel.select({0, 0}, {2, 0});
    RTF_ASSERT(sel.line_span(1, 0, from, to, eol));
    RTF_ASSERT(from == 0 && to == 0 && eol);
}

void test_text_selection::test_lines_scrolled_out()
{
    // The log keeps its last lines: line numbers 100..103 are in the list.
    text_selection sel;
    sel.select({100, 6}, {101, 5});
    RTF_ASSERT_EQUAL(selected_text(sel, log_lines, 100), string("one\nbravo"));

    // Line 100 dropped off the front and "echo five" came in: the list now
    // starts at 101. A selection from line 100 keeps what's left of it.
    deque<string> later(log_lines.begin() + 1, log_lines.end());
    later.push_back("echo five");
    sel.select({100, 6}, {102, 7});
    RTF_ASSERT_EQUAL(selected_text(sel, later, 101), string("bravo two\ncharlie"));

    // All of it gone: nothing.
    sel.select({90, 0}, {95, 3});
    RTF_ASSERT_EQUAL(selected_text(sel, later, 101), string());
    // Past the end: clamped to the last line.
    sel.select({103, 0}, {200, 4});
    RTF_ASSERT_EQUAL(selected_text(sel, later, 101), string("delta four\necho five"));
}

void test_text_selection::test_empty()
{
    text_selection sel;
    RTF_ASSERT(sel.empty());
    RTF_ASSERT_EQUAL(selected_text(sel, log_lines, 0), string());

    // A click without a drag selects nothing.
    sel.begin({1, 4});
    RTF_ASSERT(sel.empty());
    size_t from, to;
    bool eol;
    RTF_ASSERT(!sel.line_span(1, 9, from, to, eol));

    sel.extend({1, 6});
    RTF_ASSERT(!sel.empty());
    sel.clear();
    RTF_ASSERT(sel.empty());
    // extend() without begin() does nothing.
    sel.extend({2, 3});
    RTF_ASSERT(sel.empty());

    RTF_ASSERT_EQUAL(selected_text(sel, deque<string>(), 0), string());
}

void test_text_selection::test_all_text()
{
    RTF_ASSERT_EQUAL(all_text(log_lines), string("alpha one\nbravo two\ncharlie three\ndelta four\n"));
    RTF_ASSERT_EQUAL(all_text(deque<string>()), string());
}

void test_text_selection::test_col_at_x()
{
    string line = "abcdef";
    // Before the first character's middle: 0; just past it: 1.
    RTF_ASSERT_EQUAL(col_at_x(line, -20.0f, ten_per_byte), (size_t)0);
    RTF_ASSERT_EQUAL(col_at_x(line, 4.9f, ten_per_byte), (size_t)0);
    RTF_ASSERT_EQUAL(col_at_x(line, 5.0f, ten_per_byte), (size_t)1);
    RTF_ASSERT_EQUAL(col_at_x(line, 14.0f, ten_per_byte), (size_t)1);
    RTF_ASSERT_EQUAL(col_at_x(line, 26.0f, ten_per_byte), (size_t)3);
    // Past the end: the end.
    RTF_ASSERT_EQUAL(col_at_x(line, 56.0f, ten_per_byte), (size_t)6);
    RTF_ASSERT_EQUAL(col_at_x(line, 500.0f, ten_per_byte), (size_t)6);
    RTF_ASSERT_EQUAL(col_at_x(string(), 30.0f, ten_per_byte), (size_t)0);
}

void test_text_selection::test_col_at_x_utf8()
{
    // "a", "é" (2 bytes), "€" (3 bytes), "b". Each character measured whole,
    // 10 wide, so offsets land only at character starts.
    string line = "a\xC3\xA9\xE2\x82\xAC" "b";
    vector<size_t> widths_asked;
    auto per_char = [&](const char* b, const char* e) {
        widths_asked.push_back((size_t)(e - b));
        return 10.0f;
    };
    RTF_ASSERT_EQUAL(col_at_x(line, 12.0f, per_char), (size_t)1);
    RTF_ASSERT_EQUAL(col_at_x(line, 16.0f, per_char), (size_t)3);
    RTF_ASSERT_EQUAL(col_at_x(line, 26.0f, per_char), (size_t)6);
    RTF_ASSERT_EQUAL(col_at_x(line, 40.0f, per_char), (size_t)7);
    for(auto n : widths_asked)
        RTF_ASSERT(n >= 1 && n <= 3);
    widths_asked.clear();
    col_at_x(line, 100.0f, per_char);
    RTF_ASSERT(widths_asked == (vector<size_t>{1, 2, 3, 1}));
}
