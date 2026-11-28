#include "test_r_storage_summary.h"
#include "r_vss/r_storage_summary.h"

using namespace r_vss;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_storage_summary);

static const int64_t DAY = 86400;

void test_r_storage_summary::test_no_cameras()
{
    auto s = summarize_storage({});
    RTF_ASSERT_EQUAL(s.recordings_bytes, 0);
    RTF_ASSERT_EQUAL(s.cameras, 0);
    RTF_ASSERT(s.main_dir.empty());
    RTF_ASSERT(s.retention_days_min == 0.0 && s.retention_days_max == 0.0);
}

void test_r_storage_summary::test_sums_and_days()
{
    // 10 days at 100 bytes/s, and 20 days at 50 bytes/s.
    auto s = summarize_storage({{"/v", 10 * DAY * 100, 100}, {"/v", 20 * DAY * 50, 50}});
    RTF_ASSERT_EQUAL(s.recordings_bytes, 10 * DAY * 100 + 20 * DAY * 50);
    RTF_ASSERT_EQUAL(s.cameras, 2);
    RTF_ASSERT(s.retention_days_min > 9.999 && s.retention_days_min < 10.001);
    RTF_ASSERT(s.retention_days_max > 19.999 && s.retention_days_max < 20.001);
}

void test_r_storage_summary::test_main_dir_is_the_one_with_most_reserved()
{
    auto s = summarize_storage({{"/a", 100, 1}, {"/b", 500, 1}, {"/a", 300, 1}});
    RTF_ASSERT_EQUAL(s.main_dir, string("/b"));
    s = summarize_storage({{"/a", 100, 1}, {"/b", 500, 1}, {"/a", 450, 1}});
    RTF_ASSERT_EQUAL(s.main_dir, string("/a"));
    // Everything counts toward the total, wherever it is.
    RTF_ASSERT_EQUAL(s.recordings_bytes, 1050);
}

void test_r_storage_summary::test_idle_and_empty_cameras_have_no_days()
{
    // Not receiving (0 bytes/s) or no file yet: counted, but no days.
    auto s = summarize_storage({{"/v", 1000, 0}, {"/v", 0, 50}, {"/v", -5, 50}});
    RTF_ASSERT_EQUAL(s.cameras, 3);
    RTF_ASSERT_EQUAL(s.recordings_bytes, 1000);
    RTF_ASSERT(s.retention_days_min == 0.0 && s.retention_days_max == 0.0);
    // One that has both sets the range alone.
    s = summarize_storage({{"/v", 1000, 0}, {"/v", 2 * DAY * 10, 10}});
    RTF_ASSERT(s.retention_days_min > 1.999 && s.retention_days_min < 2.001);
    RTF_ASSERT(s.retention_days_max == s.retention_days_min);
}
