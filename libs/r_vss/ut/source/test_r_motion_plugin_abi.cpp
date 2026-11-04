#include "test_r_motion_plugin_abi.h"
#include "r_vss/r_motion_event_plugin_host.h"
#include <string>

using namespace r_vss;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_motion_plugin_abi);

namespace
{
int current() { return R_MOTION_PLUGIN_ABI_VERSION; }
int older() { return R_MOTION_PLUGIN_ABI_VERSION - 1; }
}

void test_r_motion_plugin_abi::test_a_matching_plugin_loads()
{
    RTF_ASSERT(motion_plugin_abi_problem(current).empty());
}

// A plugin built against other headers would use the host's objects (an
// r_detection, the host itself) with the wrong layout: it's refused, and the
// log says why and what to do.
void test_r_motion_plugin_abi::test_a_stale_plugin_is_refused()
{
    auto none = motion_plugin_abi_problem(nullptr);
    RTF_ASSERT(none.find("built before motion plugins carried an ABI version") != string::npos);
    RTF_ASSERT(none.find("rebuild it with this Revere") != string::npos);

    auto old = motion_plugin_abi_problem(older);
    RTF_ASSERT(old.find("ABI version " + to_string(R_MOTION_PLUGIN_ABI_VERSION - 1)) != string::npos);
    RTF_ASSERT(old.find("this Revere's is " + to_string(R_MOTION_PLUGIN_ABI_VERSION)) != string::npos);
}
