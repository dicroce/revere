#include "test_log_tail.h"
#include "r_ui_utils/log_tail.h"
#include "r_ui_utils/text_selection.h"

using namespace r_ui_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_log_tail);

void test_log_tail::test_keeps_last_lines()
{
    log_tail log(1000);
    log.with_lines([](const deque<string>& lines, uint64_t first) {
        RTF_ASSERT(lines.empty());
        RTF_ASSERT_EQUAL(first, (uint64_t)0);
    });

    for(int i = 0; i < 1000; ++i)
        log.add("line " + to_string(i));
    log.with_lines([](const deque<string>& lines, uint64_t first) {
        RTF_ASSERT_EQUAL(lines.size(), (size_t)1000);
        RTF_ASSERT_EQUAL(first, (uint64_t)0);
        RTF_ASSERT_EQUAL(lines.front(), string("line 0"));
    });
}

void test_log_tail::test_counts_dropped_lines()
{
    // Five past the cap: the five oldest go, and the front line is number 5.
    log_tail log(1000);
    for(int i = 0; i < 1005; ++i)
        log.add("line " + to_string(i));
    log.with_lines([](const deque<string>& lines, uint64_t first) {
        RTF_ASSERT_EQUAL(lines.size(), (size_t)1000);
        RTF_ASSERT_EQUAL(first, (uint64_t)5);
        RTF_ASSERT_EQUAL(lines.front(), string("line 5"));
        RTF_ASSERT_EQUAL(lines.back(), string("line 1004"));
    });

    // So a selection made on "line 10" before more arrive still copies
    // "line 10" after another 3 drop.
    text_selection sel;
    sel.select({10, 0}, {10, 7});
    for(int i = 1005; i < 1008; ++i)
        log.add("line " + to_string(i));
    log.with_lines([&](const deque<string>& lines, uint64_t first) {
        RTF_ASSERT_EQUAL(first, (uint64_t)8);
        RTF_ASSERT_EQUAL(selected_text(sel, lines, first), string("line 10"));
    });
}
