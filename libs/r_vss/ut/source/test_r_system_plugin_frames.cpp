#include "test_r_system_plugin_frames.h"
#include "r_vss/r_system_plugin_host.h"
#include "r_vss/r_frame_feeds.h"
#include "r_vss/r_system_plugin.h"
#include "r_utils/r_logger.h"
#include <filesystem>
#include <fstream>
#include <atomic>
#include <mutex>
#include <thread>
#include <random>
#include <sstream>
#include <vector>

using namespace r_vss;
using namespace r_utils;
using namespace std;
namespace fs = std::filesystem;

REGISTER_TEST_FIXTURE(test_r_system_plugin_frames);

static vector<string> _lines(const string& path)
{
    ifstream f(path);
    vector<string> out;
    string l;
    while(getline(f, l))
        out.push_back(l);
    return out;
}

static string _name()
{
    return fs::path(TEST_FRAMES_PLUGIN).stem().string();
}

static void _video(r_frame_feeds& feeds, const string& cam, size_t n, bool key, int64_t ts)
{
    vector<uint8_t> d(n, (uint8_t)ts);
    feeds.post_video(cam, "main", d.data(), d.size(), key, ts, "h264", "sps1");
}

static void _audio(r_frame_feeds& feeds, const string& cam, size_t n, int64_t ts)
{
    vector<uint8_t> d(n, (uint8_t)ts);
    feeds.post_audio(cam, d.data(), d.size(), ts, "aac", "aac1");
}

static auto _recording(const string& cam)
{
    return [cam](const string& id) { return id == cam; };
}

void test_r_system_plugin_frames::setup()
{
    _top = (fs::temp_directory_path() / ("r_vss_plugin_frames_" + to_string(random_device()()))).string();
    fs::create_directories(fs::path(_top) / "system_plugins");
    fs::copy_file(TEST_FRAMES_PLUGIN, fs::path(_top) / "system_plugins" / fs::path(TEST_FRAMES_PLUGIN).filename());
}

void test_r_system_plugin_frames::teardown()
{
    r_logger::clear_log_callback();
    error_code ec;
    fs::remove_all(_top, ec);
}

void test_r_system_plugin_frames::test_a_plugin_gets_the_host_functions()
{
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT_EQUAL(l.size(), (size_t)1);
    RTF_ASSERT(l[0] == "api version=1 size_ok=1");
    // Once: a second call doesn't give them again.
    host.set_frame_source(&feeds, _recording("cam1"));
    RTF_ASSERT_EQUAL(_lines(_top + "/frames.txt").size(), (size_t)1);
}

void test_r_system_plugin_frames::test_a_plugin_reads_a_cameras_frames()
{
    // Through the C functions: video from its key frame, audio beside it, in order,
    // each frame's fields as stored; subscribe and unsubscribe are logged with the
    // plugin's GUID.
    vector<string> log;
    mutex log_lock;
    r_logger::set_log_callback([&](r_logger::LOG_LEVEL, const string& msg) {
        if(msg.find("frame feed") != string::npos)
        {
            lock_guard<mutex> g(log_lock);
            log.push_back(msg);
        }
    });

    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    RTF_ASSERT(host.run_plugin_action(_name(), "sub:cam1:-:" + to_string(R_FRAME_VIDEO | R_FRAME_AUDIO)));
    RTF_ASSERT_EQUAL(feeds.feeds(), (size_t)1);
    _video(feeds, "cam1", 50, false, 1);
    _audio(feeds, "cam1", 10, 2);
    _video(feeds, "cam1", 60, true, 3);
    _video(feeds, "cam1", 40, false, 4);
    RTF_ASSERT(host.run_plugin_action(_name(), "read:3:full"));
    RTF_ASSERT(host.run_plugin_action(_name(), "unsub"));
    RTF_ASSERT_EQUAL(feeds.feeds(), (size_t)0);

    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT_EQUAL(l.size(), (size_t)6);
    RTF_ASSERT(l[1] == "sub ok");
    RTF_ASSERT(l[2] == "frame flags=" + to_string(R_FRAME_AUDIO | R_FRAME_CODEC_CHANGED) + " ts=2 size=10 codec=aac params=aac1 data0=2 stream=main");
    RTF_ASSERT(l[3] == "frame flags=" + to_string(R_FRAME_VIDEO | R_FRAME_KEY | R_FRAME_CODEC_CHANGED) + " ts=3 size=60 codec=h264 params=sps1 data0=3 stream=main");
    RTF_ASSERT(l[4] == "frame flags=" + to_string(R_FRAME_VIDEO) + " ts=4 size=40 codec=h264 params=sps1 data0=4 stream=main");
    RTF_ASSERT(l[5] == "unsub");

    lock_guard<mutex> g(log_lock);
    RTF_ASSERT_EQUAL(log.size(), (size_t)2);
    RTF_ASSERT(log[0].find("com.revere.test.frames") != string::npos && log[0].find("cam1") != string::npos && log[0].find("video") != string::npos);
    RTF_ASSERT(log[1].find("ended") != string::npos);
}

void test_r_system_plugin_frames::test_an_older_r_frame_is_not_overrun()
{
    // A plugin whose r_frame ends before the stream field gets the rest, and the host
    // writes nothing past its struct_size; a full one gets stream and nothing past it.
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    RTF_ASSERT(host.run_plugin_action(_name(), "sub:cam1:sub:" + to_string(R_FRAME_VIDEO)));
    _video(feeds, "cam1", 30, true, 5);
    _video(feeds, "cam1", 20, false, 6);
    RTF_ASSERT(host.run_plugin_action(_name(), "read:1:small"));
    RTF_ASSERT(host.run_plugin_action(_name(), "read:1:full"));
    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT_EQUAL(l.size(), (size_t)4);
    RTF_ASSERT(l[2].find(" ts=5 size=30 ") != string::npos);
    RTF_ASSERT(l[2].find("stream=unwritten") != string::npos);
    // "sub" with no substream: main's frames, saying so.
    RTF_ASSERT(l[3].find(" ts=6 ") != string::npos);
    RTF_ASSERT(l[3].find("stream=main") != string::npos && l[3].find("OVERRUN") == string::npos);
}

void test_r_system_plugin_frames::test_unknown_cameras_streams_and_media()
{
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    RTF_ASSERT(host.run_plugin_action(_name(), "sub:cam2:-:" + to_string(R_FRAME_VIDEO)));                   // not recording here
    RTF_ASSERT(host.run_plugin_action(_name(), "sub:cam1:secondary:" + to_string(R_FRAME_VIDEO)));           // no such stream
    RTF_ASSERT(host.run_plugin_action(_name(), "sub:cam1:-:" + to_string(R_FRAME_KEY)));                     // not a media bit
    RTF_ASSERT(host.run_plugin_action(_name(), "sub:cam1:main:" + to_string(R_FRAME_AUDIO)));                // fine
    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT((vector<string>(l.begin() + 1, l.end()) == vector<string>{"sub null", "sub null", "sub null", "sub ok"}));
    RTF_ASSERT_EQUAL(feeds.feeds(), (size_t)1);
}

void test_r_system_plugin_frames::test_stopping_closes_what_a_plugin_left()
{
    // A plugin that stops without unsubscribing: its feed is closed (next_frame says -1
    // at once), and freed when the host goes.
    r_frame_feeds feeds;
    {
        r_system_plugin_host host(_top);
        host.set_frame_source(&feeds, _recording("cam1"));
        RTF_ASSERT(host.run_plugin_action(_name(), "sub:cam1:-:" + to_string(R_FRAME_VIDEO)));
        _video(feeds, "cam1", 10, true, 1);
        host.stop_all();
        RTF_ASSERT(host.run_plugin_action(_name(), "next"));
        RTF_ASSERT_EQUAL(feeds.feeds(), (size_t)1);
    }
    RTF_ASSERT_EQUAL(feeds.feeds(), (size_t)0);
    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT(l.back() == "next=-1");
}

void test_r_system_plugin_frames::test_without_a_frame_source_no_functions()
{
    // A host nobody gave a frame source (an older caller): the plugin gets no table.
    r_system_plugin_host host(_top);
    RTF_ASSERT(host.run_plugin_action(_name(), "sub:cam1:-:1"));
    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT_EQUAL(l.size(), (size_t)1);
    RTF_ASSERT(l[0] == "no api");
}

void test_r_system_plugin_frames::test_a_small_bound_still_fits_a_key_frame()
{
    // A plugin asking for a 1000-byte queue gets 1 MB: a 200 KB key frame still comes.
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    RTF_ASSERT(host.run_plugin_action(_name(), "sub:cam1:-:" + to_string(R_FRAME_VIDEO) + ":1000"));
    _video(feeds, "cam1", 200000, true, 7);
    RTF_ASSERT(host.run_plugin_action(_name(), "read:1:full"));
    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT(l.back().find(" ts=7 size=200000 ") != string::npos);
}

void test_r_system_plugin_frames::test_at_most_16_feeds_a_plugin()
{
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    RTF_ASSERT(host.run_plugin_action(_name(), "subs:cam1:20"));
    RTF_ASSERT(_lines(_top + "/frames.txt").back() == "subs got=16");
    RTF_ASSERT_EQUAL(feeds.feeds(), (size_t)16);
}

void test_r_system_plugin_frames::test_unsubscribe_wakes_a_waiting_reader()
{
    // A plugin thread waiting in next_frame while another unsubscribes: it returns -1 at
    // once, and the feed is freed only after it has.
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    RTF_ASSERT(host.run_plugin_action(_name(), "sub:cam1:-:" + to_string(R_FRAME_VIDEO)));
    RTF_ASSERT(host.run_plugin_action(_name(), "wake"));
    RTF_ASSERT(_lines(_top + "/frames.txt").back() == "wake=-1 prompt");
    RTF_ASSERT_EQUAL(feeds.feeds(), (size_t)0);
}

void test_r_system_plugin_frames::test_an_old_handle_reads_as_closed()
{
    // next_frame on a handle already unsubscribed says closed, not whatever is in freed memory.
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    RTF_ASSERT(host.run_plugin_action(_name(), "sub:cam1:-:" + to_string(R_FRAME_VIDEO)));
    _video(feeds, "cam1", 10, true, 1);
    RTF_ASSERT(host.run_plugin_action(_name(), "stale"));
    RTF_ASSERT(_lines(_top + "/frames.txt").back() == "stale=-1");
}

void test_r_system_plugin_frames::test_concurrent_subscribes_keep_the_cap()
{
    // Eight threads subscribing at once still get 16 feeds between them, not more.
    for(int round = 0; round < 20; ++round)
    {
        r_frame_feeds feeds;
        r_system_plugin_host host(_top);
        host.set_frame_source(&feeds, _recording("cam1"));
        RTF_ASSERT(host.run_plugin_action(_name(), "race:cam1:8:4"));
        RTF_ASSERT(_lines(_top + "/frames.txt").back() == "race got=16");
        RTF_ASSERT_EQUAL(feeds.feeds(), (size_t)16);
    }
}

void test_r_system_plugin_frames::test_unsubscribe_races_a_reading_thread()
{
    // A plugin thread reading frames as fast as they come while another unsubscribes it,
    // 200 times: every reader ends with -1, and (under ASan) nothing it reads was freed.
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    atomic<bool> stop {false};
    thread poster([&]() {
        int64_t ts = 0;
        while(!stop)
        {
            _video(feeds, "cam1", 2000, (ts % 10) == 0, ts);
            _audio(feeds, "cam1", 200, ts);
            ++ts;
        }
    });
    auto ok = host.run_plugin_action(_name(), "churn:cam1:200");
    stop = true;
    poster.join();
    RTF_ASSERT(ok);
    RTF_ASSERT(_lines(_top + "/frames.txt").back() == "churn frames=some ended=200");
    RTF_ASSERT_EQUAL(feeds.feeds(), (size_t)0);
}
