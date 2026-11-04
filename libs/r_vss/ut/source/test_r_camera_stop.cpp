#include "test_r_camera_stop.h"
#include "r_vss/r_query.h"
#include "r_disco/r_devices.h"
#include "r_disco/r_stream_config.h"
#include <chrono>
#include <filesystem>

using namespace r_vss;
using namespace r_disco;
using namespace std;
namespace fs = std::filesystem;

REGISTER_TEST_FIXTURE(test_r_camera_stop);

void test_r_camera_stop::setup()
{
    _top = (fs::temp_directory_path() / ("r_camera_stop_ut_" + to_string(chrono::steady_clock::now().time_since_epoch().count()))).string();
    fs::create_directories(_top);
}

void test_r_camera_stop::teardown()
{
    error_code ec;
    fs::remove_all(_top, ec);
}

static void _discover(r_devices& devices, const string& id)
{
    r_stream_config sc;
    sc.id = id;
    sc.ipv4 = "10.0.0.5";
    sc.rtsp_url = "rtsp://10.0.0.5/stream1";
    devices.insert_or_update_devices({{sc, hash_stream_config(sc)}});
}

void test_r_camera_stop::test_stop_and_start_an_assigned_camera()
{
    // Stopping keeps the camera assigned with its settings; starting clears it;
    // asking for the state it's already in changes nothing.
    r_devices devices(_top);
    devices.start();
    _discover(devices, "cam-1");
    auto camera = devices.get_camera_by_id("cam-1").value();
    camera.friendly_name.set_value("Porch");
    camera.min_continuous_recording_hours.set_value(48);
    devices.assign_camera(camera);

    query_set_camera_stopped(devices, "cam-1", true);
    auto stopped = devices.get_camera_by_id("cam-1").value();
    RTF_ASSERT(stopped.stopped);
    RTF_ASSERT(stopped.state == "assigned");
    RTF_ASSERT(stopped.friendly_name.value() == "Porch");
    RTF_ASSERT_EQUAL(stopped.min_continuous_recording_hours.value(), 48);

    query_set_camera_stopped(devices, "cam-1", true);
    RTF_ASSERT(devices.get_camera_by_id("cam-1").value().stopped);

    query_set_camera_stopped(devices, "cam-1", false);
    auto started = devices.get_camera_by_id("cam-1").value();
    RTF_ASSERT(!started.stopped);
    RTF_ASSERT(started.state == "assigned");
    devices.stop();
}

void test_r_camera_stop::test_only_assigned_cameras_stop()
{
    // An unknown camera, or one only discovered, can't be stopped.
    r_devices devices(_top);
    devices.start();
    _discover(devices, "cam-2");
    bool unknown = false, discovered = false;
    try { query_set_camera_stopped(devices, "no-such-camera", true); } catch(const std::exception&) { unknown = true; }
    try { query_set_camera_stopped(devices, "cam-2", true); } catch(const std::exception&) { discovered = true; }
    RTF_ASSERT(unknown);
    RTF_ASSERT(discovered);
    RTF_ASSERT(!devices.get_camera_by_id("cam-2").value().stopped);
    devices.stop();
}
