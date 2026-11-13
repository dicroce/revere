#include "test_r_system_plugin_motion.h"
#include "r_vss/r_system_plugin_host.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

using namespace r_vss;
using namespace std;
using namespace std::chrono;
namespace fs = std::filesystem;

REGISTER_TEST_FIXTURE(test_r_system_plugin_motion);

namespace
{

string read_file(const string& path)
{
    ifstream f(path);
    stringstream s;
    s << f.rdbuf();
    return s.str();
}

// The file's content once it is `want`, or whatever it holds after 5 s.
string eventually_reads(const string& path, const string& want)
{
    auto end = steady_clock::now() + seconds(5);
    string got;
    while(steady_clock::now() < end)
    {
        got = read_file(path);
        if(got == want)
            return got;
        this_thread::sleep_for(milliseconds(5));
    }
    return got;
}

}

void test_r_system_plugin_motion::setup()
{
    _top = (fs::temp_directory_path() / ("r_vss_plugin_motion_" + to_string(random_device()()))).string();
    fs::create_directories(fs::path(_top) / "system_plugins");
}

void test_r_system_plugin_motion::teardown()
{
    error_code ec;
    fs::remove_all(_top, ec);
}

static void install(const string& top, const char* plugin)
{
    fs::copy_file(plugin, fs::path(top) / "system_plugins" / fs::path(plugin).filename());
}

void test_r_system_plugin_motion::test_plugin_gets_motion_start_end_and_state_changes()
{
    install(_top, TEST_MOTION_PLUGIN);
    r_system_plugin_host host(_top);
    host.start_all();

    host.notify_motion("cam1", true, 1000);
    host.notify_camera_state("cam1", true, false);
    host.notify_motion("cam1", false, 2500);
    host.notify_camera_state("cam1", false, true);
    host.notify_motion("cam2", true, 3000);

    RTF_ASSERT_EQUAL(eventually_reads(_top + "/motion.txt", "cam1 1 1000\ncam1 0 2500\ncam2 1 3000\n"),
                     string("cam1 1 1000\ncam1 0 2500\ncam2 1 3000\n"));
    RTF_ASSERT_EQUAL(eventually_reads(_top + "/state.txt", "cam1 1 0\ncam1 0 1\n"), string("cam1 1 0\ncam1 0 1\n"));
}

void test_r_system_plugin_motion::test_a_plugin_started_later_gets_each_cameras_latest_state()
{
    install(_top, TEST_MOTION_PLUGIN);
    r_system_plugin_host host(_top);

    // The keeper knows the cameras before the plugin is started: it hears each one's latest, and no motion from before.
    host.notify_camera_state("cam1", false, false);
    host.notify_camera_state("cam1", true, false);
    host.notify_camera_state("cam2", false, true);
    host.notify_motion("cam1", true, 1);
    host.start_all();

    RTF_ASSERT_EQUAL(eventually_reads(_top + "/state.txt", "cam1 1 0\ncam2 0 1\n"), string("cam1 1 0\ncam2 0 1\n"));
    this_thread::sleep_for(milliseconds(50));
    RTF_ASSERT(read_file(_top + "/motion.txt").empty());
}

void test_r_system_plugin_motion::test_a_forgotten_camera_is_not_replayed()
{
    install(_top, TEST_MOTION_PLUGIN);
    r_system_plugin_host host(_top);
    host.notify_camera_state("cam1", true, false);
    host.notify_camera_state("cam2", true, false);
    host.forget_camera_state("cam1");
    host.start_all();
    RTF_ASSERT_EQUAL(eventually_reads(_top + "/state.txt", "cam2 1 0\n"), string("cam2 1 0\n"));
}

void test_r_system_plugin_motion::test_nothing_before_start_or_after_stop()
{
    install(_top, TEST_MOTION_PLUGIN);
    r_system_plugin_host host(_top);
    host.notify_motion("cam1", true, 1);
    host.start_all();
    host.notify_motion("cam1", true, 2);
    RTF_ASSERT_EQUAL(eventually_reads(_top + "/motion.txt", "cam1 1 2\n"), string("cam1 1 2\n"));

    host.stop_all();
    host.notify_motion("cam1", false, 3);
    host.notify_camera_state("cam1", true, true);
    this_thread::sleep_for(milliseconds(100));
    RTF_ASSERT_EQUAL(read_file(_top + "/motion.txt"), string("cam1 1 2\n"));
    RTF_ASSERT(read_file(_top + "/state.txt").empty());
}

void test_r_system_plugin_motion::test_a_plugin_without_the_exports_loads_and_is_left_alone()
{
    install(_top, TEST_NO_MOTION_PLUGIN);
    r_system_plugin_host host(_top);
    RTF_ASSERT_EQUAL(host.get_loaded_plugins().size(), (size_t)1);
    host.start_all();
    host.notify_camera_state("cam1", true, false);
    host.notify_motion("cam1", true, 1);
    host.notify_motion("cam1", false, 2);
    this_thread::sleep_for(milliseconds(100));
    RTF_ASSERT(read_file(_top + "/motion.txt").empty());
    RTF_ASSERT(read_file(_top + "/state.txt").empty());
}

void test_r_system_plugin_motion::test_a_slow_plugin_does_not_hold_up_the_caller()
{
    install(_top, TEST_MOTION_PLUGIN);
    r_system_plugin_host host(_top);
    host.start_all();

    // Each of these takes the plugin half a second: 40 of them is 20 s if the caller waited for them.
    auto start = steady_clock::now();
    for(int i = 0; i < 20; ++i)
    {
        host.notify_motion("slow1", true, i);
        host.notify_camera_state("slow1", i % 2 == 0, false);
    }
    RTF_ASSERT(steady_clock::now() - start < seconds(1));
}
