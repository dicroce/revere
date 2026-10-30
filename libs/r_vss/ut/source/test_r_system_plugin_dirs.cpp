#include "test_r_system_plugin_dirs.h"
#include "r_vss/r_system_plugin_host.h"
#include "r_utils/r_file.h"
#include <string>

using namespace r_vss;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_system_plugin_dirs);

static const string EXE = "/opt/revere/bin";
static const string TOP = "/home/u/Documents/revere/revere";

static string sp(const string& dir)
{
    return dir + PATH_SLASH + "system_plugins";
}

void test_r_system_plugin_dirs::test_not_a_snap()
{
    // Next to the executable, then the user data folder, as before.
    auto d = system_plugin_dirs(EXE, TOP, nullptr, nullptr);
    RTF_ASSERT(d.load.size() == 2);
    RTF_ASSERT(d.load[0] == sp(EXE));
    RTF_ASSERT(d.load[1] == sp(TOP));
    RTF_ASSERT(d.unloadable.empty());

    // $SNAP_USER_COMMON alone (no $SNAP) is not the Snap.
    d = system_plugin_dirs(EXE, TOP, nullptr, "/home/u/snap/revere/common");
    RTF_ASSERT(d.load.size() == 2);
    RTF_ASSERT(d.load[1] == sp(TOP));
    RTF_ASSERT(d.unloadable.empty());
}

void test_r_system_plugin_dirs::test_snap()
{
    // Under the Snap: $SNAP_USER_COMMON/system_plugins replaces the user data
    // folder, which is only checked for a plugin left behind.
    auto d = system_plugin_dirs("/snap/revere/42/bin", TOP, "/snap/revere/42", "/home/u/snap/revere/common");
    RTF_ASSERT(d.load.size() == 2);
    RTF_ASSERT(d.load[0] == sp("/snap/revere/42/bin"));
    RTF_ASSERT(d.load[1] == sp("/home/u/snap/revere/common"));
    RTF_ASSERT(d.unloadable == sp(TOP));
}

void test_r_system_plugin_dirs::test_snap_without_user_common()
{
    // snapd always sets $SNAP_USER_COMMON; without it (unset or empty), keep
    // today's folders rather than guess.
    auto d = system_plugin_dirs(EXE, TOP, "/snap/revere/42", nullptr);
    RTF_ASSERT(d.load.size() == 2);
    RTF_ASSERT(d.load[1] == sp(TOP));
    RTF_ASSERT(d.unloadable.empty());

    d = system_plugin_dirs(EXE, TOP, "/snap/revere/42", "");
    RTF_ASSERT(d.load.size() == 2);
    RTF_ASSERT(d.load[1] == sp(TOP));
    RTF_ASSERT(d.unloadable.empty());
}
