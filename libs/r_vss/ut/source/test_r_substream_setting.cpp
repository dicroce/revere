#include "test_r_substream_setting.h"
#include "r_vss/r_substream_setting.h"
#include "r_utils/r_file.h"
#include <filesystem>
#include <fstream>

using namespace r_vss;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_substream_setting);

// Anything but a clear "enabled": true is off.
void test_r_substream_setting::test_json()
{
    RTF_ASSERT(parse_substream_setting(substream_setting_json(true)));
    RTF_ASSERT(!parse_substream_setting(substream_setting_json(false)));
    RTF_ASSERT(!parse_substream_setting(""));
    RTF_ASSERT(!parse_substream_setting("not json"));
    RTF_ASSERT(!parse_substream_setting("{}"));
    RTF_ASSERT(!parse_substream_setting("{\"enabled\": \"true\"}"));
    RTF_ASSERT(!parse_substream_setting("{\"enabled\": 1}"));
    RTF_ASSERT(!parse_substream_setting("[true]"));
}

// A new computer, an unreadable file: off. Turned on, it stays on for the next start.
void test_r_substream_setting::test_off_until_turned_on_and_it_persists()
{
    auto dir = (filesystem::temp_directory_path() / "r_substream_setting_test").string();
    filesystem::remove_all(dir);
    filesystem::create_directories(dir);
    {
        r_substream_setting s(dir);
        RTF_ASSERT(!s.enabled());
        RTF_ASSERT(!s.set_enabled(false));    // no change
        RTF_ASSERT(s.set_enabled(true));
        RTF_ASSERT(s.enabled());
    }
    {
        r_substream_setting s(dir);
        RTF_ASSERT(s.enabled());
        RTF_ASSERT(s.set_enabled(false));
    }
    {
        r_substream_setting s(dir);
        RTF_ASSERT(!s.enabled());
    }
    {
        ofstream f(dir + PATH_SLASH + "config" + PATH_SLASH + "substreams.json");
        f << "garbage";
    }
    {
        r_substream_setting s(dir);
        RTF_ASSERT(!s.enabled());
    }
    filesystem::remove_all(dir);
}
