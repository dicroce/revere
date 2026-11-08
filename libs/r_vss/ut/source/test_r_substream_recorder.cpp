#include "test_r_substream_recorder.h"
#include "r_vss/r_substream_recorder.h"
#include "r_fakey/r_fake_camera.h"
#include "r_storage/r_storage_file_reader.h"
#include "r_vss/r_query.h"
#include "r_disco/r_devices.h"
#include "r_utils/r_blob_tree.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include <chrono>
#include <memory>
#include <thread>

using namespace std;
using namespace std::chrono;
using namespace r_vss;
using namespace r_disco;
using namespace r_utils;

REGISTER_TEST_FIXTURE(test_r_substream_recorder);

static const string RING_DIR = "substream_ut";

static void _remove_dir()
{
    vector<string> files = {"front_sub.nts", "none_sub.nts"};
    for(auto f : {"cameras.db", "cameras.db-wal", "cameras.db-shm"})
        files.push_back("db" + PATH_SLASH + f);
    for(auto& f : files)
    {
        auto p = RING_DIR + PATH_SLASH + f;
        if(r_fs::file_exists(p))
            r_fs::remove_file(p);
    }
}

void test_r_substream_recorder::setup()
{
    if(!r_fs::file_exists(RING_DIR))
        r_fs::mkdir(RING_DIR);
    _remove_dir();
}

void test_r_substream_recorder::teardown()
{
    _remove_dir();
}

void test_r_substream_recorder::test_ring_path()
{
    RTF_ASSERT(substream_ring_path("/video/front_door.nts") == "/video/front_door_sub.nts");
    RTF_ASSERT(substream_ring_path("C:\\video\\front.door.nts") == "C:\\video\\front.door_sub.nts");
    // No extension, and a dot only in a directory name.
    RTF_ASSERT(substream_ring_path("/video/front") == "/video/front_sub");
    RTF_ASSERT(substream_ring_path("/vid.eo/front") == "/vid.eo/front_sub");
}

void test_r_substream_recorder::test_next_step()
{
    // wanted, main_receiving, running, running_dead, backoff_over
    RTF_ASSERT(substream_next_step(true, true, false, false, true) == substream_step::start);
    RTF_ASSERT(substream_next_step(true, true, false, false, false) == substream_step::none);   // backoff
    RTF_ASSERT(substream_next_step(true, false, false, false, true) == substream_step::none);   // main first
    RTF_ASSERT(substream_next_step(false, true, false, false, true) == substream_step::none);   // not chosen
    RTF_ASSERT(substream_next_step(true, true, true, false, true) == substream_step::none);    // running fine
    RTF_ASSERT(substream_next_step(true, true, true, true, false) == substream_step::stop);    // dead
    RTF_ASSERT(substream_next_step(false, true, true, false, true) == substream_step::stop);   // turned off
    RTF_ASSERT(substream_next_step(true, false, true, false, true) == substream_step::stop);   // main stopped receiving
}

// A live H.264 substream on loopback: 320x240 at 15 fps, a keyframe every 15 frames. Null without x264enc.
static shared_ptr<r_fakey::r_fake_camera> _substream_camera()
{
    // Nothing has started GStreamer yet in this process; without it the factory is never found and the test skips.
    gst_init(nullptr, nullptr);
    auto x264 = gst_element_factory_find("x264enc");
    if(!x264)
        return nullptr;
    gst_object_unref(x264);
    auto fc = make_shared<r_fakey::r_fake_camera>(
        "/sub",
        "( videotestsrc is-live=true pattern=ball ! video/x-raw,width=320,height=240,framerate=15/1 ! "
        "x264enc key-int-max=15 bframes=0 speed-preset=ultrafast ! video/x-h264,profile=constrained-baseline ! "
        "rtph264pay name=pay0 pt=96 config-interval=-1 )",
        0);
    thread([fc]() { fc->start(); }).detach();
    return fc;
}

static r_camera _camera(const string& url)
{
    r_camera c;
    c.id = "cam-sub";
    c.friendly_name = string("Front");
    c.sub_mode = string("on");
    c.sub_status = string("ok");
    c.sub_rtsp_url = url;
    return c;
}

void test_r_substream_recorder::test_records_into_its_ring()
{
    auto fc = _substream_camera();
    if(!fc)
        return;
    while(fc->bound_port() < 0)
        this_thread::sleep_for(milliseconds(10));

    auto ring = RING_DIR + PATH_SLASH + "front_sub.nts";
    ensure_substream_ring(ring);
    RTF_ASSERT(r_fs::file_exists(ring));
    {
        r_substream_recorder rec(_camera(r_string_utils::format("rtsp://127.0.0.1:%d/sub", fc->bound_port())), ring, "tcp");
        RTF_ASSERT(!rec.fresh());   // nothing yet
        auto deadline = steady_clock::now() + seconds(15);
        while(rec.frames_written() < 20 && steady_clock::now() < deadline)
            this_thread::sleep_for(milliseconds(100));
        RTF_ASSERT(rec.answered());
        RTF_ASSERT(rec.frames_written() >= 20);
        RTF_ASSERT(rec.fresh());
        RTF_ASSERT(!rec.dead());
    }
    fc->quit();

    // An existing ring is kept as it is.
    ensure_substream_ring(ring);
    {
        r_storage::r_storage_file_reader reader(ring);
        RTF_ASSERT(!reader.query_segments().empty());
    }

    // /video?stream=sub reads it: the camera records to front.nts, so its ring is front_sub.nts.
    r_devices devices(RING_DIR);
    devices.start();
    r_stream_config sc;
    sc.id = "cam-sub";
    sc.ipv4 = "127.0.0.1";
    sc.rtsp_url = "rtsp://127.0.0.1/main";
    devices.insert_or_update_devices({{sc, hash_stream_config(sc)}});
    auto cam = devices.get_camera_by_id("cam-sub").value();
    cam.record_file_path = RING_DIR + PATH_SLASH + "front.nts";
    devices.save_camera(cam);

    auto now = system_clock::now();
    auto video = query_get_video(RING_DIR, devices, "cam-sub", now - minutes(5), now, true);
    uint32_t version = 0;
    auto bt = r_blob_tree::deserialize(video.data(), video.size(), version);
    RTF_ASSERT(bt["frames"].size() >= 15);
    RTF_ASSERT(bt["video_codec_name"].get_string() == "h264");

    // A camera with no ring: not found (a 404), not some other error.
    cam.record_file_path = RING_DIR + PATH_SLASH + "back.nts";
    devices.save_camera(cam);
    bool not_found = false;
    try
    {
        query_get_video(RING_DIR, devices, "cam-sub", now - minutes(5), now, true);
    }
    catch(const r_not_found_exception&)
    {
        not_found = true;
    }
    RTF_ASSERT(not_found);
    devices.stop();
}

void test_r_substream_recorder::test_no_camera()
{
    // Nothing listens on port 1: the camera never answers, nothing is written, and it isn't fresh.
    auto ring = RING_DIR + PATH_SLASH + "none_sub.nts";
    ensure_substream_ring(ring);
    r_substream_recorder rec(_camera("rtsp://127.0.0.1:1/sub"), ring, "tcp");
    this_thread::sleep_for(seconds(2));
    RTF_ASSERT(!rec.answered());
    RTF_ASSERT_EQUAL(rec.frames_written(), 0);
    RTF_ASSERT(!rec.fresh());
    RTF_ASSERT(!rec.dead());   // not until SUBSTREAM_DEAD_MS without video
}
