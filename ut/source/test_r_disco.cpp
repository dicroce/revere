
#include "test_r_disco.h"
#include "utils.h"
#include "r_pipeline/r_gst_source.h"
#include "r_pipeline/r_arg.h"
#include "r_pipeline/r_stream_info.h"
#include "r_fakey/r_fake_camera.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_work_q.h"
#include "r_utils/r_std_utils.h"
#include "r_utils/r_file.h"
#include "r_disco/r_agent.h"
#include "r_disco/r_devices.h"

#include <sys/types.h>
#include <thread>
#include <vector>
#include <functional>
#include <algorithm>
#include <iterator>
#include <utility>
#include <tuple>

#ifdef IS_WINDOWS
#include <Windows.h>
#include <tchar.h> 
#include <stdio.h>
#include <strsafe.h>
#pragma warning( push )
#pragma warning( disable : 4244 )
#endif
#include <gst/gst.h>
#ifdef IS_WINDOWS
#pragma warning( pop )
#endif
#ifdef IS_LINUX
#include <dirent.h>
#endif

using namespace std;
using namespace r_pipeline;
using namespace r_fakey;
using namespace r_utils;

REGISTER_TEST_FIXTURE(test_r_disco);

using namespace r_disco;

void test_r_disco::setup()
{
    gst_init(NULL, NULL);

    teardown();

    r_fs::mkdir("top_dir");
    r_fs::mkdir("top_dir" + PATH_SLASH + "config");
    r_fs::mkdir("top_dir" + PATH_SLASH + "db");
}

void test_r_disco::teardown()
{
    auto cfg_json_path = "top_dir" + PATH_SLASH + "config" + PATH_SLASH + "manual_config.json";
    if(r_fs::file_exists(cfg_json_path))
        r_fs::remove_file(cfg_json_path);
    auto cfg_path = "top_dir" + PATH_SLASH + "config";
    if(r_fs::file_exists(cfg_path))
        r_fs::rmdir(cfg_path);
    if(r_fs::file_exists("top_dir" + PATH_SLASH + "db" + PATH_SLASH + "cameras.db"))
        r_fs::remove_file("top_dir" + PATH_SLASH + "db" + PATH_SLASH + "cameras.db");
    if(r_fs::file_exists("top_dir" + PATH_SLASH + "db"))
        r_fs::rmdir("top_dir" + PATH_SLASH + "db");
    if(r_fs::file_exists("top_dir"))
        r_fs::rmdir("top_dir");
}

void test_r_disco::test_r_disco_r_agent_start_stop()
{
    int port = RTF_NEXT_PORT();

    auto fc = _create_fc(port, "root", "1234");

    auto fct = thread([&](){
        fc->start();
    });

    r_agent agent("top_dir");
    agent.set_credential_cb([](const string& id){return make_pair(r_nullable<string>(), r_nullable<string>());});
    agent.start();

    this_thread::sleep_for(chrono::milliseconds(5000));
    agent.stop();

    fc->quit();
    fct.join();
}

vector<pair<r_stream_config, string>> _fake_stream_configs()
{
    vector<pair<r_stream_config, string>> configs;

    r_stream_config cfg;
    cfg.id = "93950da6-fc12-493c-a051-c22a9fec3440";
    cfg.camera_name = "fake_cam1";
    cfg.ipv4 = "127.0.0.1";
    cfg.rtsp_url = "rtsp://url";
    cfg.video_codec = "h264";
    cfg.video_timebase = 90000;
    cfg.audio_codec = "mp4a-latm";
    cfg.audio_timebase = 48000;
    configs.push_back(make_pair(cfg, hash_stream_config(cfg)));

    cfg.id = "27d0f031-da8d-41a0-9687-5fd689a78bec";
    cfg.camera_name = "fake_cam2";
    cfg.ipv4 = "127.0.0.1";
    cfg.rtsp_url = "rtsp://url";
    cfg.video_codec = "h265";
    cfg.video_timebase = 90000;
    cfg.audio_codec = "pcmu";
    cfg.audio_timebase = 8000;
    configs.push_back(make_pair(cfg, hash_stream_config(cfg)));

    cfg.id = "fae9db8f-08a5-48b7-a22d-1c19a0c05c4f";
    cfg.camera_name = "fake_cam3";
    cfg.ipv4 = "127.0.0.1";
    cfg.rtsp_url = "rtsp://url";
    cfg.video_codec = "h265";
    cfg.video_codec_parameters = "foo=bar";
    cfg.video_timebase = 90000;
    cfg.audio_codec = "mp4a-latm";
    cfg.audio_codec_parameters = "foo=bar";
    cfg.audio_timebase = 8000;
    configs.push_back(make_pair(cfg, hash_stream_config(cfg)));

    return configs;
}

void test_r_disco::test_r_disco_r_devices_basic()
{
    r_devices devices("top_dir");
    devices.start();

    devices.insert_or_update_devices(_fake_stream_configs());

    auto all_cameras = devices.get_all_cameras();

    RTF_ASSERT(all_cameras.size() == 3);
    RTF_ASSERT(all_cameras[0].id == "93950da6-fc12-493c-a051-c22a9fec3440");
    RTF_ASSERT(all_cameras[0].camera_name == "fake_cam1");
    RTF_ASSERT(all_cameras[0].state == "discovered");
    RTF_ASSERT(all_cameras[1].id == "27d0f031-da8d-41a0-9687-5fd689a78bec");
    RTF_ASSERT(all_cameras[1].camera_name == "fake_cam2");
    RTF_ASSERT(all_cameras[1].state == "discovered");

    all_cameras[1].rtsp_username = "root";
    all_cameras[1].rtsp_password = "1234";
    all_cameras[1].state = "assigned";
    devices.save_camera(all_cameras[1]);

    auto all_assigned_cameras = devices.get_assigned_cameras();

    RTF_ASSERT(all_assigned_cameras.size() == 1);

    RTF_ASSERT(all_assigned_cameras[0].id == "27d0f031-da8d-41a0-9687-5fd689a78bec");
    RTF_ASSERT(all_assigned_cameras[0].camera_name == "fake_cam2");
    RTF_ASSERT(all_assigned_cameras[0].state == "assigned");
    RTF_ASSERT(all_assigned_cameras[0].rtsp_username == "root");
    RTF_ASSERT(all_assigned_cameras[0].rtsp_password == "1234");

    devices.stop();
}

void test_r_disco::test_r_disco_r_devices_modified()
{
    // Note: this test is simulating the behavior of a camera CHANGING to rtsp_url. This is not
    // something an application using r_disco would do.
    r_devices devices("top_dir");
    devices.start();

    devices.insert_or_update_devices(_fake_stream_configs());

    auto all_before = devices.get_all_cameras();

    all_before[1].rtsp_url = "rtsp://new_url";

    devices.save_camera(all_before[1]);

    auto modified = devices.get_modified_cameras(all_before);

    RTF_ASSERT(modified.size() == 1);
    RTF_ASSERT(modified[0].id == "27d0f031-da8d-41a0-9687-5fd689a78bec");

    all_before = devices.get_all_cameras();

    // now, save without modifying anything...
    devices.save_camera(all_before[1]);

    modified = devices.get_modified_cameras(all_before);

    RTF_ASSERT(modified.size() == 0);
}

void test_r_disco::test_r_disco_r_devices_assigned_cameras_added()
{
    r_devices devices("top_dir");
    devices.start();

    devices.insert_or_update_devices(_fake_stream_configs());

    auto all_before = devices.get_all_cameras();

    vector<pair<r_stream_config, string>> configs;

    r_stream_config cfg;
    cfg.id = "9d807570-3d0e-4f87-9773-ae8d6471eab6";
    cfg.ipv4 = "127.0.0.1";
    cfg.rtsp_url = "rtsp://url";
    cfg.video_codec = "h265";
    cfg.video_timebase = 90000;
    cfg.audio_codec = "aac";
    cfg.audio_timebase = 48000;
    configs.push_back(make_pair(cfg, hash_stream_config(cfg)));

    devices.insert_or_update_devices(configs);

    auto c = devices.get_camera_by_id("9d807570-3d0e-4f87-9773-ae8d6471eab6").value();

    c.state = "assigned";

    devices.save_camera(c);

    auto assigned_added = devices.get_assigned_cameras_added(all_before);

    RTF_ASSERT(assigned_added.size() == 1);

    RTF_ASSERT(assigned_added.front().id == "9d807570-3d0e-4f87-9773-ae8d6471eab6");

    auto all_after = devices.get_all_cameras();

    assigned_added = devices.get_assigned_cameras_added(all_after);

    RTF_ASSERT(assigned_added.size() == 0);
}

void test_r_disco::test_r_disco_r_devices_assigned_cameras_removed()
{
    r_devices devices("top_dir");
    devices.start();

    devices.insert_or_update_devices(_fake_stream_configs());

    auto all_before = devices.get_all_cameras();

    vector<pair<r_stream_config, string>> configs;

    r_stream_config cfg;
    cfg.id = "9d807570-3d0e-4f87-9773-ae8d6471eab6";
    cfg.ipv4 = "127.0.0.1";
    cfg.rtsp_url = "rtsp://url";
    cfg.video_codec = "h265";
    cfg.video_timebase = 90000;
    cfg.audio_codec = "aac";
    cfg.audio_timebase = 48000;
    configs.push_back(make_pair(cfg, hash_stream_config(cfg)));

    devices.insert_or_update_devices(configs);

    auto c = devices.get_camera_by_id("9d807570-3d0e-4f87-9773-ae8d6471eab6").value();

    c.state = "assigned";

    devices.save_camera(c);

    auto assigned_before = devices.get_assigned_cameras();

    RTF_ASSERT(assigned_before.size() == 1);

    devices.remove_camera(c);

    auto removed = devices.get_assigned_cameras_removed(assigned_before);

    RTF_ASSERT(removed.size() == 1);
    RTF_ASSERT(removed.front().id == "9d807570-3d0e-4f87-9773-ae8d6471eab6");

    auto assigned_after = devices.get_assigned_cameras();

    RTF_ASSERT(assigned_after.size() == 0);
}
