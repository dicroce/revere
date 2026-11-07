#include "test_r_failure_reason.h"
#include "r_vss/r_query.h"

using namespace r_vss;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_failure_reason);

void test_r_failure_reason::test_first_line_only()
{
    // what() is the message, then the stack.
    RTF_ASSERT(r_failure_reason("Failed to open codec libx264: Generic error\n[#9] r_exception @ (in libr_utils.dylib)\n[#8] x") ==
               "Failed to open codec libx264: Generic error");
    RTF_ASSERT(r_failure_reason("") == "");
}

void test_r_failure_reason::test_source_position_dropped()
{
    // A nanots exception begins with where it was thrown, a path on the machine that built it.
    RTF_ASSERT(r_failure_reason("/Users/someone/projects/revere/build/_deps/nanots-src/nanots.cpp:1835: Unable to find block\n[#1] stack") ==
               "Unable to find block");
    RTF_ASSERT(r_failure_reason("C:\\build\\_deps\\nanots-src\\nanots.cpp:77: Unable to find block") == "Unable to find block");
    RTF_ASSERT(r_failure_reason("/home/b/nanots.cpp:12 bad") == "bad");
}

void test_r_failure_reason::test_other_paths_cut_to_last_part()
{
    RTF_ASSERT(r_failure_reason("Failed to open /Users/someone/.local/share/revere/cam1.nts for reading") ==
               "Failed to open cam1.nts for reading");
    // Not a path: a ratio, a time, a single slash.
    RTF_ASSERT(r_failure_reason("window 10/20 at 12:30 failed") == "window 10/20 at 12:30 failed");
}
