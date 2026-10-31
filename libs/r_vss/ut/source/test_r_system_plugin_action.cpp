#include "test_r_system_plugin_action.h"
#include "r_vss/r_system_plugin_host.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

using namespace r_vss;
using namespace std;
namespace fs = std::filesystem;

REGISTER_TEST_FIXTURE(test_r_system_plugin_action);

static string _read(const string& path)
{
    ifstream f(path);
    stringstream s;
    s << f.rdbuf();
    return s.str();
}

void test_r_system_plugin_action::setup()
{
    // A top_dir of its own, with both test plugins in its system_plugins folder.
    _top = (fs::temp_directory_path() / ("r_vss_plugin_action_" + to_string(random_device()()))).string();
    fs::create_directories(fs::path(_top) / "system_plugins");
    for(auto p : {TEST_ACTION_PLUGIN, TEST_NO_ACTION_PLUGIN})
        fs::copy_file(p, fs::path(_top) / "system_plugins" / fs::path(p).filename());
}

void test_r_system_plugin_action::teardown()
{
    error_code ec;
    fs::remove_all(_top, ec);
}

void test_r_system_plugin_action::test_runs_the_plugins_action()
{
    r_system_plugin_host host(_top);
    auto name = fs::path(TEST_ACTION_PLUGIN).stem().string();
    RTF_ASSERT(host.run_plugin_action(name, "reset:cam1"));
    RTF_ASSERT(host.run_plugin_action(name, "other"));
    RTF_ASSERT(_read(_top + "/actions.txt") == "reset:cam1\nother\n");
}

void test_r_system_plugin_action::test_no_export_or_no_plugin()
{
    r_system_plugin_host host(_top);
    RTF_ASSERT(!host.run_plugin_action(fs::path(TEST_NO_ACTION_PLUGIN).stem().string(), "reset:cam1"));
    RTF_ASSERT(!host.run_plugin_action("not_loaded", "reset:cam1"));
    RTF_ASSERT(!fs::exists(_top + "/actions.txt"));
}
