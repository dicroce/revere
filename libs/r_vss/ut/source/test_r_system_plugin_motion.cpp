#include "test_r_system_plugin_motion.h"
#include "r_vss/r_system_plugin_host.h"
#include "r_vss/r_motion_event_plugin_host.h"
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

void test_r_system_plugin_motion::test_a_plugin_loaded_after_start_gets_each_cameras_latest_state()
{
    // The host is running, with no plugin yet; the keeper has told it about two cameras.
    r_system_plugin_host host(_top);
    host.start_all();
    host.notify_camera_state("cam1", false, false);
    host.notify_camera_state("cam1", true, false);
    host.notify_camera_state("cam2", false, true);

    string why;
    RTF_ASSERT(host.load_and_start(TEST_MOTION_PLUGIN, why));
    RTF_ASSERT_EQUAL(eventually_reads(_top + "/state.txt", "cam1 1 0\ncam2 0 1\n"), string("cam1 1 0\ncam2 0 1\n"));

    // And it takes what comes after.
    host.notify_motion("cam1", true, 7);
    RTF_ASSERT_EQUAL(eventually_reads(_top + "/motion.txt", "cam1 1 7\n"), string("cam1 1 7\n"));
}

void test_r_system_plugin_motion::test_stop_all_waits_for_a_call_in_the_plugin()
{
    install(_top, TEST_MOTION_PLUGIN);
    r_system_plugin_host host(_top);
    host.start_all();

    // "slow" calls take the plugin half a second, and write their line at the end. One that is running when stop_all
    // is called is finished by the time it returns; the ones behind it never run.
    host.notify_motion("slow1", true, 1);
    host.notify_motion("slow2", true, 2);
    this_thread::sleep_for(milliseconds(150));
    host.stop_all();
    RTF_ASSERT_EQUAL(read_file(_top + "/motion.txt"), string("slow1 1 1\n"));
    this_thread::sleep_for(milliseconds(700));
    RTF_ASSERT_EQUAL(read_file(_top + "/motion.txt"), string("slow1 1 1\n"));
}

void test_r_system_plugin_motion::test_stop_all_gives_up_on_a_call_that_does_not_return()
{
    install(_top, TEST_MOTION_PLUGIN);
    r_system_plugin_host host(_top);
    host.set_stop_wait(milliseconds(100));
    host.start_all();

    // A "slow" call takes the plugin half a second: longer than stop_all is willing to wait, so it goes on without it.
    host.notify_motion("slow1", true, 1);
    this_thread::sleep_for(milliseconds(150));
    auto start = steady_clock::now();
    host.stop_all();
    auto took = steady_clock::now() - start;
    RTF_ASSERT(took >= milliseconds(90));
    RTF_ASSERT(took < milliseconds(400));
    RTF_ASSERT(read_file(_top + "/motion.txt").empty());   // the call was still running
}

void test_r_system_plugin_motion::test_destroying_the_host_leaves_a_plugin_in_a_call_loaded()
{
    install(_top, TEST_MOTION_PLUGIN);
    auto host = make_unique<r_system_plugin_host>(_top);
    host->set_stop_wait(milliseconds(100));
    host->start_all();

    // A "slow" call takes the plugin half a second, so it is still inside the plugin when the host is destroyed.
    host->notify_motion("slow1", true, 1);
    this_thread::sleep_for(milliseconds(150));
    auto start = steady_clock::now();
    host.reset();
    auto took = steady_clock::now() - start;
    const string at_once = read_file(_top + "/motion.txt");

    // The call returns into the plugin's code after the host is gone: had the library been closed it would run on
    // unmapped code, and this process would not be here to read the line it writes at the end.
    const string later = eventually_reads(_top + "/motion.txt", "slow1 1 1\n");
    RTF_ASSERT(at_once.empty());   // still in the call when the host was gone
    RTF_ASSERT(took < seconds(2));   // and destroying the host did not wait for it
    RTF_ASSERT_EQUAL(later, string("slow1 1 1\n"));
}

void test_r_system_plugin_motion::test_destroying_the_host_leaves_a_plugin_in_a_detection_loaded()
{
    install(_top, TEST_MOTION_PLUGIN);
    auto host = make_unique<r_system_plugin_host>(_top);
    host->set_stop_wait(milliseconds(100));
    host->start_all();

    // A detection for a "slow" camera blocks in the plugin for half a second, on a thread of its own as the bus's would.
    r_detection det;
    det.camera_id = "slow1";
    det.ts = 5;
    det.class_name = "person";
    det.confidence = 0.9f;
    det.x1 = det.y1 = det.x2 = det.y2 = 0.0f;
    thread caller([&]() { host->dispatch_detection(det, 5); });
    this_thread::sleep_for(milliseconds(150));

    auto start = steady_clock::now();
    host.reset();
    auto took = steady_clock::now() - start;
    // The plugin is still inside the call: it was not destroyed (destroyed.txt is its destroy running) and its code is
    // still there to return into. Look now, and let the call finish before judging.
    const bool destroyed = fs::exists(fs::path(_top) / "destroyed.txt");
    const string at_once = read_file(_top + "/detection.txt");
    caller.join();
    const string later = read_file(_top + "/detection.txt");
    RTF_ASSERT(!destroyed);
    RTF_ASSERT(at_once.empty());
    RTF_ASSERT(took < seconds(2));
    RTF_ASSERT_EQUAL(later, string("slow1 5 person\n"));
    RTF_ASSERT(!fs::exists(fs::path(_top) / "destroyed.txt"));
}

void test_r_system_plugin_motion::test_a_late_detection_does_not_go_on_to_the_next_plugin()
{
    // Two plugins, each of which takes half a second over a detection for a "slow" camera and writes a line when it has
    // it. The host is destroyed while the first is still inside; the second has not been called and its library is
    // closed by the time the first returns, so it must not be called then: one line, not two.
    install(_top, TEST_MOTION_PLUGIN);
    install(_top, TEST_NO_MOTION_PLUGIN);
    auto host = make_unique<r_system_plugin_host>(_top);
    host->set_stop_wait(milliseconds(100));
    host->start_all();

    r_detection det;
    det.camera_id = "slow1";
    det.ts = 6;
    det.class_name = "person";
    det.confidence = 0.9f;
    det.x1 = det.y1 = det.x2 = det.y2 = 0.0f;
    thread caller([&]() { host->dispatch_detection(det, 6); });
    this_thread::sleep_for(milliseconds(150));
    host.reset();
    caller.join();
    // Give a wrongly made second call time to write its line.
    this_thread::sleep_for(milliseconds(700));
    // detection_calls.txt has a line for each call as it started (it needs no handle, so a call into the second plugin
    // after its destroy shows too, unless the process is gone with its library).
    const string calls = read_file(_top + "/detection_calls.txt");
    const string lines = read_file(_top + "/detection.txt");
    RTF_ASSERT_EQUAL(calls, string("slow1\n"));
    RTF_ASSERT_EQUAL(lines, string("slow1 6 person\n"));
}
