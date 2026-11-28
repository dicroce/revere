#include "test_r_system_plugin_frames.h"
#include "r_vss/r_system_plugin_host.h"
#include "r_vss/r_plugin_states.h"
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
    RTF_ASSERT(l[0] == "api version=3 size_ok=1");
    // Once: a second call doesn't give them again.
    host.set_frame_source(&feeds, _recording("cam1"));
    RTF_ASSERT_EQUAL(_lines(_top + "/frames.txt").size(), (size_t)1);
}

// get_pressure (host API version 3): revere's pressure JSON, copied snprintf-style into the plugin's buffer.
void test_r_system_plugin_frames::test_a_plugin_reads_revere_pressure()
{
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    const string json = "{\"level\":\"elevated\"}";

    // No source set: negative (a plugin then carries on as if ok).
    RTF_ASSERT(host.run_plugin_action(_name(), "pressure:100"));
    // A source set after the plugin has its table reaches it.
    host.set_pressure_source([&]() { return json; });
    RTF_ASSERT(host.run_plugin_action(_name(), "pressure:100"));
    // The length asked for with no buffer; and a buffer too small: cut, NUL-terminated, the full length returned.
    RTF_ASSERT(host.run_plugin_action(_name(), "pressure:0"));
    RTF_ASSERT(host.run_plugin_action(_name(), "pressure:8"));
    // Exactly the length: cut by one (it needs the NUL); one more fits.
    RTF_ASSERT(host.run_plugin_action(_name(), "pressure:" + to_string(json.size())));
    RTF_ASSERT(host.run_plugin_action(_name(), "pressure:" + to_string(json.size() + 1)));
    // A source that throws is no pressure.
    host.set_pressure_source([]() -> string { throw runtime_error("no"); });
    RTF_ASSERT(host.run_plugin_action(_name(), "pressure:100"));

    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT_EQUAL(l.size(), (size_t)8);
    RTF_ASSERT(l[1] == "pressure n=-1 guard=ok text=");
    RTF_ASSERT(l[2] == "pressure n=" + to_string(json.size()) + " guard=ok text=" + json);
    RTF_ASSERT(l[3] == "pressure n=" + to_string(json.size()) + " guard=ok text=");
    RTF_ASSERT(l[4] == "pressure n=" + to_string(json.size()) + " guard=ok text=" + json.substr(0, 7));
    RTF_ASSERT(l[5] == "pressure n=" + to_string(json.size()) + " guard=ok text=" + json.substr(0, json.size() - 1));
    RTF_ASSERT(l[6] == "pressure n=" + to_string(json.size()) + " guard=ok text=" + json);
    RTF_ASSERT(l[7] == "pressure n=-1 guard=ok text=");
}

// Pressure with no frame source at all: the plugin still has its table, and its frame functions find nothing.
void test_r_system_plugin_frames::test_pressure_without_a_frame_source()
{
    r_system_plugin_host host(_top);
    host.set_pressure_source([]() { return string("{}"); });
    RTF_ASSERT(host.run_plugin_action(_name(), "pressure:10"));
    RTF_ASSERT(host.run_plugin_action(_name(), "sub:cam1:-:" + to_string(R_FRAME_VIDEO)));
    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT_EQUAL(l.size(), (size_t)3);
    RTF_ASSERT(l[0] == "api version=3 size_ok=1");
    RTF_ASSERT(l[1] == "pressure n=2 guard=ok text={}");
    RTF_ASSERT(l[2] == "sub null");
}

static string _cloud_name()
{
    return fs::path(TEST_FRAMES_CLOUD_PLUGIN).stem().string();
}

static void _add_plugin(const string& top, const char* path)
{
    fs::copy_file(path, fs::path(top) / "system_plugins" / fs::path(path).filename());
}

// set_plugin_state (host API version 3): a plugin's whole state, replaced on each change, with a version that moves only
// when something did.
void test_r_system_plugin_frames::test_a_plugin_publishes_its_state()
{
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    const string guid = "com.revere.test.frames";

    // Nothing yet: version 0, no plugins.
    RTF_ASSERT(host.get_plugin_states_json() == "{\"plugins\":{},\"version\":0}");
    RTF_ASSERT(host.run_plugin_action(_name(), "state:{\"v\":1,\"mode\":\"live\"}"));
    RTF_ASSERT_EQUAL(host.plugin_states_version(), (int64_t)1);
    // The same JSON written another way (key order, spacing): nothing changed, the version stays.
    RTF_ASSERT(host.run_plugin_action(_name(), "state:{ \"mode\" : \"live\", \"v\" : 1 }"));
    RTF_ASSERT_EQUAL(host.plugin_states_version(), (int64_t)1);
    // A change moves it by one.
    RTF_ASSERT(host.run_plugin_action(_name(), "state:{\"v\":1,\"mode\":\"off\"}"));
    RTF_ASSERT_EQUAL(host.plugin_states_version(), (int64_t)2);
    // Refused: not an object, not JSON, empty; the last state stays and the version doesn't move.
    RTF_ASSERT(host.run_plugin_action(_name(), "state:[1,2]"));
    RTF_ASSERT(host.run_plugin_action(_name(), "state:not json"));
    RTF_ASSERT(host.run_plugin_action(_name(), "state:"));
    RTF_ASSERT_EQUAL(host.plugin_states_version(), (int64_t)2);
    // Over 256 KB: refused too.
    {
        const string big = "{\"x\":\"" + string(r_plugin_states::MAX_STATE_BYTES, 'a') + "\"}";
        r_plugin_states st;
        RTF_ASSERT_EQUAL(st.set("g", big), -1);
        RTF_ASSERT_EQUAL(st.version(), (int64_t)0);
    }
    RTF_ASSERT(host.get_plugin_states_json() == "{\"plugins\":{\"" + guid + "\":{\"mode\":\"off\",\"v\":1}},\"version\":2}");

    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT_EQUAL(l.size(), (size_t)7);
    RTF_ASSERT(l[1] == "state r=0" && l[2] == "state r=0" && l[3] == "state r=0");
    RTF_ASSERT(l[4] == "state r=-1" && l[5] == "state r=-1" && l[6] == "state r=-1");
}

// get_plugin_states through the C table: only the cloud plugin may read; the length, a cut, and no buffer.
void test_r_system_plugin_frames::test_only_the_cloud_plugin_reads_the_states()
{
    _add_plugin(_top, TEST_FRAMES_CLOUD_PLUGIN);
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    RTF_ASSERT(host.run_plugin_action(_name(), "state:{\"v\":1}"));
    const string expected = "{\"plugins\":{\"com.revere.test.frames\":{\"v\":1}},\"version\":1}";

    RTF_ASSERT(host.run_plugin_action(_cloud_name(), "states:0"));
    RTF_ASSERT(host.run_plugin_action(_cloud_name(), "states:10"));
    RTF_ASSERT(host.run_plugin_action(_cloud_name(), "states:" + to_string(expected.size() + 1)));
    // Another plugin gets nothing: -1, and its buffer is untouched.
    RTF_ASSERT(host.run_plugin_action(_name(), "states:100"));

    auto c = _lines(_top + "/cloud.txt");
    RTF_ASSERT_EQUAL(c.size(), (size_t)4);
    RTF_ASSERT(c[1] == "states n=" + to_string(expected.size()) + " guard=ok text=");
    RTF_ASSERT(c[2] == "states n=" + to_string(expected.size()) + " guard=ok text=" + expected.substr(0, 9));
    RTF_ASSERT(c[3] == "states n=" + to_string(expected.size()) + " guard=ok text=" + expected);
    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT(l.back() == "states n=-1 guard=ok text=");
}

// plugin_action: queued for the UI's thread, by GUID; only the cloud plugin may ask; the answer says it was queued.
void test_r_system_plugin_frames::test_a_plugin_action_is_queued_for_the_ui_thread()
{
    _add_plugin(_top, TEST_ACTION_PLUGIN);
    _add_plugin(_top, TEST_NO_ACTION_PLUGIN);
    _add_plugin(_top, TEST_FRAMES_CLOUD_PLUGIN);
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));

    const string act = "{\"action\":\"set_mode\",\"camera\":\"c1\"}";
    RTF_ASSERT(host.plugin_action("com.revere.test.action", act) == "{\"ok\":true}");
    // Queued, not run: the plugin has not been called on this thread.
    RTF_ASSERT(!fs::exists(_top + "/actions.txt"));
    RTF_ASSERT_EQUAL(host.run_queued_plugin_actions(), (size_t)1);
    // The plugin was given the action JSON as its string, once.
    auto actions = _lines(_top + "/actions.txt");
    RTF_ASSERT_EQUAL(actions.size(), (size_t)1);
    RTF_ASSERT(actions[0] == act);
    // No such plugin; one with no action function; an action that isn't a JSON object: nothing queued.
    RTF_ASSERT(host.plugin_action("com.revere.test.nobody", act) == "{\"error\":\"no such plugin\",\"ok\":false}");
    RTF_ASSERT(host.plugin_action("com.revere.test.no_action", act) == "{\"error\":\"the plugin takes no actions\",\"ok\":false}");
    RTF_ASSERT(host.plugin_action("com.revere.test.action", "try_again") == "{\"error\":\"the action isn't a JSON object\",\"ok\":false}");
    RTF_ASSERT(host.plugin_action("com.revere.test.action", "[1]") == "{\"error\":\"the action isn't a JSON object\",\"ok\":false}");
    RTF_ASSERT_EQUAL(host.run_queued_plugin_actions(), (size_t)0);

    // From the cloud plugin, through the C table: the result in its buffer, queued, run later on the UI's thread.
    RTF_ASSERT(host.run_plugin_action(_cloud_name(), "act:com.revere.test.action:" + act));
    RTF_ASSERT(host.run_plugin_action(_cloud_name(), "act:com.revere.test.nobody:" + act));
    RTF_ASSERT(host.run_plugin_action(_cloud_name(), "act:com.revere.test.frames:" + act));
    RTF_ASSERT_EQUAL(_lines(_top + "/actions.txt").size(), (size_t)1);          // not yet
    RTF_ASSERT_EQUAL(host.run_queued_plugin_actions(), (size_t)2);
    RTF_ASSERT_EQUAL(_lines(_top + "/actions.txt").size(), (size_t)2);
    auto c = _lines(_top + "/cloud.txt");
    RTF_ASSERT(c[1] == "act n=11 text={\"ok\":true}");
    RTF_ASSERT(c[2] == "act n=37 text={\"error\":\"no such plugin\",\"ok\":false}");
    RTF_ASSERT(c[3] == "act n=11 text={\"ok\":true}");
    // The frames plugin was handed its action (it notes a JSON action).
    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT(l.back() == "json action " + act);

    // Any other plugin: not allowed, and nothing is queued.
    RTF_ASSERT(host.run_plugin_action(_name(), "act:com.revere.test.action:" + act));
    RTF_ASSERT_EQUAL(host.run_queued_plugin_actions(), (size_t)0);
    l = _lines(_top + "/frames.txt");
    RTF_ASSERT(l.back() == "act n=34 text={\"error\":\"not allowed\",\"ok\":false}");
    RTF_ASSERT_EQUAL(_lines(_top + "/actions.txt").size(), (size_t)2);
}

// A plugin that is disabled or stopped has no state: it is dropped (the version rises), a disabled plugin can't set one,
// and enabling it lets it again.
void test_r_system_plugin_frames::test_a_disabled_or_stopped_plugin_has_no_state()
{
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    RTF_ASSERT(host.run_plugin_action(_name(), "state:{\"v\":1}"));
    RTF_ASSERT_EQUAL(host.plugin_states_version(), (int64_t)1);

    host.set_plugin_enabled(_name(), false);
    RTF_ASSERT(host.get_plugin_states_json() == "{\"plugins\":{},\"version\":2}");
    // Disabled: its state is refused.
    RTF_ASSERT(host.run_plugin_action(_name(), "state:{\"v\":2}"));
    RTF_ASSERT_EQUAL(host.plugin_states_version(), (int64_t)2);
    // Enabled again: it may.
    host.set_plugin_enabled(_name(), true);
    RTF_ASSERT(host.run_plugin_action(_name(), "state:{\"v\":2}"));
    RTF_ASSERT_EQUAL(host.plugin_states_version(), (int64_t)3);
    RTF_ASSERT(host.get_plugin_states_json() == "{\"plugins\":{\"com.revere.test.frames\":{\"v\":2}},\"version\":3}");

    // Stopped (as when the host is going): dropped again, and it can't set one meanwhile.
    host.stop_all();
    RTF_ASSERT(host.get_plugin_states_json() == "{\"plugins\":{},\"version\":4}");
    RTF_ASSERT(host.run_plugin_action(_name(), "state:{\"v\":3}"));
    RTF_ASSERT_EQUAL(host.plugin_states_version(), (int64_t)4);

    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT(l[1] == "state r=0" && l[2] == "state r=-1" && l[3] == "state r=0" && l[4] == "state r=-1");
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

void test_r_system_plugin_frames::test_a_plugin_starts_at_the_newest_key_frame()
{
    // Version 2: subscribe_frames_from with R_SUBSCRIBE_FROM_NEWEST_KEY, given the GOP at
    // the next frame; a flag the host doesn't know is refused.
    r_frame_feeds feeds;
    r_system_plugin_host host(_top);
    host.set_frame_source(&feeds, _recording("cam1"));
    RTF_ASSERT(host.run_plugin_action(_name(), "subfrom:cam1:-:" + to_string(R_FRAME_VIDEO) + ":0:" + to_string(1u << 7)));
    RTF_ASSERT(host.run_plugin_action(_name(), "subfrom:cam1:-:" + to_string(R_FRAME_VIDEO) + ":0:" + to_string(R_SUBSCRIBE_FROM_NEWEST_KEY)));
    RTF_ASSERT(feeds.gop_wanted());
    vector<vector<uint8_t>> store = {vector<uint8_t>(60, 3), vector<uint8_t>(40, 4)};
    vector<r_gop_frame> gop = {{store[0].data(), 60, true, 3, true}, {store[1].data(), 40, false, 4, true}};
    feeds.post_video("cam1", "main", store[1].data(), 40, false, 4, "h264", "sps1", &gop);
    RTF_ASSERT(host.run_plugin_action(_name(), "read:2:full"));
    RTF_ASSERT(host.run_plugin_action(_name(), "unsub"));
    auto l = _lines(_top + "/frames.txt");
    RTF_ASSERT_EQUAL(l.size(), (size_t)6);
    RTF_ASSERT(l[1] == "sub null");
    RTF_ASSERT(l[2] == "sub ok");
    RTF_ASSERT(l[3] == "frame flags=" + to_string(R_FRAME_VIDEO | R_FRAME_KEY | R_FRAME_CODEC_CHANGED) + " ts=3 size=60 codec=h264 params=sps1 data0=3 stream=main");
    RTF_ASSERT(l[4] == "frame flags=" + to_string(R_FRAME_VIDEO) + " ts=4 size=40 codec=h264 params=sps1 data0=4 stream=main");
}
