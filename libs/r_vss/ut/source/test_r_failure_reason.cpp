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

void test_r_failure_reason::test_url_credentials_removed()
{
    // A camera's password never reaches an error a user reads.
    RTF_ASSERT(r_failure_reason("rtsp://admin:secret@10.0.0.5:554/h264 refused") == "rtsp://10.0.0.5:554/h264 refused");
    RTF_ASSERT(r_failure_reason("Failed to open rtsp://admin:p@ss/w:rd@cam.local/stream1.") == "Failed to open rtsp://cam.local/stream1.");
    RTF_ASSERT(r_failure_reason("http://user@host/x") == "http://host/x");
    // A secret in the query, or written as name=value.
    RTF_ASSERT(r_failure_reason("GET http://cam/api?user=a&password=hunter2&x=1 failed") == "GET http://cam/api?user=a&password=***&x=1 failed");
    RTF_ASSERT(r_failure_reason("login failed, pwd=hunter2 for cam") == "login failed, pwd=*** for cam");
    RTF_ASSERT(r_failure_reason("keyframe=12 ok") == "keyframe=12 ok");
}

void test_r_failure_reason::test_url_keeps_its_host_and_port()
{
    RTF_ASSERT(r_failure_reason("http://cam.local:554/stream") == "http://cam.local:554/stream");
    RTF_ASSERT(r_failure_reason("connect to rtsp://10.0.0.5:554/Streaming/Channels/101 timed out") ==
               "connect to rtsp://10.0.0.5:554/Streaming/Channels/101 timed out");
    // A path beside a URL is still cut.
    RTF_ASSERT(r_failure_reason("saved /Users/someone/clips/a.mp4 from http://cam.local:554/s") == "saved a.mp4 from http://cam.local:554/s");
}

void test_r_failure_reason::test_path_with_spaces()
{
    RTF_ASSERT(r_failure_reason("/Users/someone/Library/Application Support/Revere/revere/video/cam1.nts") == "cam1.nts");
    RTF_ASSERT(r_failure_reason("C:\\Program Files\\Revere\\plugins\\x.dll: load failed") == "x.dll: load failed");
    RTF_ASSERT(r_failure_reason("/Users/someone/Application Support/nanots.cpp:77: Unable to find block") == "Unable to find block");
    // The sentence around a path is not taken into it.
    RTF_ASSERT(r_failure_reason("Failed to open /Users/someone/Application Support/cam1.nts for reading") == "Failed to open cam1.nts for reading");
    RTF_ASSERT(r_failure_reason("copy /a/b and /c/d/e.txt failed") == "copy b and e.txt failed");
    RTF_ASSERT(r_failure_reason("either and/or/else") == "either and/or/else");
}
