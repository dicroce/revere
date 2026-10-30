#include "test_tray_status.h"
#include "tray_status.h"
#include "r_utils/3rdparty/json/json.h"

using namespace revere;
using namespace std;
using json = nlohmann::json;

REGISTER_TEST_FIXTURE(test_tray_status);

static json _camera(const string& name, const string& state, bool receiving, bool failed)
{
    return {
        {"friendly_name", name},
        {"camera_name", "IPC-" + name},
        {"state", state},
        {"receiving_video", receiving},
        {"stream_failed", failed}
    };
}

static string _cameras(const vector<json>& cams)
{
    return json{{"cameras", cams}}.dump();
}

void test_tray_status::test_unreachable()
{
    auto st = summarize_cameras(nullopt);
    RTF_ASSERT(!st.reachable);
    RTF_ASSERT(st.problem);
    RTF_ASSERT(st.summary == "Revere service isn't running");
}

void test_tray_status::test_bad_json()
{
    auto st = summarize_cameras(string("<html>"));
    RTF_ASSERT(st.reachable);
    RTF_ASSERT(st.problem);
    RTF_ASSERT(st.detail == "Couldn't read camera status");
}

void test_tray_status::test_no_assigned_cameras()
{
    auto st = summarize_cameras(_cameras({_camera("Porch", "discovered", false, false)}));
    RTF_ASSERT(st.reachable);
    RTF_ASSERT(!st.problem);
    RTF_ASSERT(st.summary == "Revere is running");
    RTF_ASSERT(st.detail == "No cameras set up yet");
}

void test_tray_status::test_one_recording()
{
    auto st = summarize_cameras(_cameras({
        _camera("Driveway", "assigned", true, false),
        _camera("Spare", "discovered", false, false)
    }));
    RTF_ASSERT(!st.problem);
    RTF_ASSERT(st.summary == "Recording 1 camera");
    RTF_ASSERT(st.detail.empty());
}

void test_tray_status::test_all_recording()
{
    auto st = summarize_cameras(_cameras({
        _camera("Driveway", "assigned", true, false),
        _camera("Porch", "assigned", true, false)
    }));
    RTF_ASSERT(!st.problem);
    RTF_ASSERT(st.summary == "Recording all 2 cameras");
}

void test_tray_status::test_some_failed()
{
    auto st = summarize_cameras(_cameras({
        _camera("Driveway", "assigned", true, false),
        _camera("Porch", "assigned", false, true)
    }));
    RTF_ASSERT(st.problem);
    RTF_ASSERT(st.summary == "Recording 1 of 2 cameras");
    RTF_ASSERT(st.detail == "Not recording: Porch");
}

void test_tray_status::test_connecting()
{
    auto st = summarize_cameras(_cameras({
        _camera("Driveway", "assigned", false, false),
        _camera("", "assigned", true, false)
    }));
    RTF_ASSERT(!st.problem);
    RTF_ASSERT(st.summary == "Recording 1 of 2 cameras");
    RTF_ASSERT(st.detail == "Connecting: Driveway");
}

void test_tray_status::test_many_failed_names()
{
    auto st = summarize_cameras(_cameras({
        _camera("A", "assigned", false, true),
        _camera("B", "assigned", false, true),
        _camera("C", "assigned", false, true),
        _camera("D", "assigned", false, true)
    }));
    RTF_ASSERT(st.problem);
    RTF_ASSERT(st.summary == "Recording 0 of 4 cameras");
    RTF_ASSERT(st.detail == "Not recording: A, B and 2 more");
}
