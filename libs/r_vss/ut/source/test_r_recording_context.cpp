
#include "test_r_recording_context.h"
#include "r_vss/r_recording_context.h"
#include "r_disco/r_camera.h"
#include "r_vss/r_frame_feeds.h"
#include "r_pipeline/r_gst_source.h"
#include "r_utils/r_string_utils.h"
#include <deque>
#include <fstream>
#include <sstream>
#include <vector>

using namespace std;
using namespace r_vss;
using namespace r_disco;

REGISTER_TEST_FIXTURE(test_r_recording_context);

void test_r_recording_context::test_source_name_prefix()
{
    r_camera camera;
    camera.id = "cam-1234";

    // No friendly name: the camera id
    RTF_ASSERT(recording_source_name_prefix(camera) == "cam-1234_");

    camera.friendly_name.set_value(string());
    RTF_ASSERT(recording_source_name_prefix(camera) == "cam-1234_");

    // A friendly name: used as is
    camera.friendly_name.set_value("Raccoon Cam 5");
    RTF_ASSERT(recording_source_name_prefix(camera) == "Raccoon Cam 5_");
}

void test_r_recording_context::test_a_feed_starts_from_the_stamped_gop_cache()
{
    // The cache as the video callback keeps it: each frame pushed before its write, then
    // stamped with the time storage gave it. A re-anchored source (stored ts not pts) keeps
    // storage's time in the burst.
    r_pipeline::gstreamer_init();
    std::deque<_gop_cache_entry> cache;
    auto push = [&](uint8_t v, size_t n, bool key, int64_t pts) {
        std::vector<uint8_t> bytes(n, v);
        cache.push_back({pts, key, r_pipeline::r_gst_buffer(bytes.data(), bytes.size())});
    };
    push(1, 100, true, 10);  stamp_newest_gop_frame(cache, 5010);
    push(2, 50, false, 11);  stamp_newest_gop_frame(cache, 5011);
    push(3, 40, false, 12);  stamp_newest_gop_frame(cache, 5012);

    std::vector<r_pipeline::r_gst_buffer::r_map_info> maps;
    auto gop = gop_for_feeds(cache, maps);
    RTF_ASSERT_EQUAL(gop.size(), (size_t)3);
    RTF_ASSERT(gop[0].key && gop[0].stored && gop[0].ts == 5010 && gop[0].size == 100 && gop[0].data[0] == 1);
    RTF_ASSERT(!gop[2].key && gop[2].ts == 5012 && gop[2].data[0] == 3);

    r_frame_feeds feeds;
    auto id = feeds.subscribe("cam1", "", r_frame_feeds::VIDEO, 0, true);
    feeds.post_video("cam1", "main", gop.back().data, gop.back().size, false, 5012, "h264", "sps1", &gop);
    r_feed_frame f;
    std::vector<int64_t> ts;
    while(feeds.next(id, 0, f) == 1)
        ts.push_back(f.ts);
    RTF_ASSERT((ts == std::vector<int64_t>{5010, 5011, 5012}));

    // A frame whose write threw is never stamped: no GOP, the next key frame.
    push(4, 30, false, 13);
    auto gop2 = gop_for_feeds(cache, maps);
    RTF_ASSERT(!gop2.back().stored);
    auto id2 = feeds.subscribe("cam1", "", r_frame_feeds::VIDEO, 0, true);
    push(5, 30, false, 14);  stamp_newest_gop_frame(cache, 5014);
    auto gop3 = gop_for_feeds(cache, maps);
    feeds.post_video("cam1", "main", gop3.back().data, gop3.back().size, false, 5014, "h264", "sps1", &gop3);
    RTF_ASSERT(feeds.next(id2, 0, f) == 0);
    RTF_ASSERT(!feeds.gop_wanted());
}

void test_r_recording_context::test_the_video_callback_stamps_the_gop_cache()
{
    // The wiring, read from the source (no test here runs a camera's pipeline): in the video
    // callback, the cache's newest frame is stamped after its write, and the GOP built from
    // the cache, for this camera's waiting feeds, goes with the feed post. Without the stamp,
    // no feed would ever start at the newest key frame, and nothing would say so.
    std::ifstream in(std::string(R_VSS_SOURCE_DIR) + "/r_recording_context.cpp");
    RTF_ASSERT(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    auto src = ss.str();
    auto write = src.find("R_STORAGE_MEDIA_TYPE_VIDEO,\n                mi.data(),");
    RTF_ASSERT(write != std::string::npos);
    auto stamp = src.find("_video_fanout.stamp_newest(stored_ts);", write);
    auto wanted = src.find("if(this->_sk->feeds_want_gop(this->_camera.id))", write);
    auto built = src.find("gop = _video_fanout.gop_for_feeds(gop_maps);", write);
    auto post = src.find("this->_sk->post_video_to_feeds(", write);
    auto passed = src.find("gop.empty() ? nullptr : &gop", write);
    RTF_ASSERT(stamp != std::string::npos && wanted != std::string::npos && built != std::string::npos);
    RTF_ASSERT(post != std::string::npos && passed != std::string::npos);
    RTF_ASSERT(stamp < wanted && wanted < built && built < post && post < passed);
    // And the push to the cache (r_live_video_fanout::post) comes before the write, so the stamp lands on this frame.
    auto pushed = src.find("_video_fanout.post(pts, key, buffer,");
    RTF_ASSERT(pushed != std::string::npos && pushed < write);
}

void test_r_recording_context::test_audio_off_stops_audio_before_storage_feeds_and_restream()
{
    // The wiring, read from the source like the test above (a recording context can't be run here: it needs a stream
    // keeper, whose web API listens on the recorder's own ports as it's made). What the gate does is
    // test_route_audio_sample's; this is that the callback goes through it: the session takes the policy before its
    // callbacks are set, and the audio callback's write context, storage write, feed post and restream queueing are
    // all inside the sinks it hands route_audio_sample with the session's _records_audio. The SDP and the restream
    // mount leave audio out too.
    std::ifstream in(std::string(R_VSS_SOURCE_DIR) + "/r_recording_context.cpp");
    RTF_ASSERT(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    auto src = ss.str();
    auto taken = src.find("_records_audio = sk ? sk->records_audio(camera.id) : true;");
    auto cb = src.find("_source.set_audio_sample_cb([this](const sample_context& sc, const r_gst_buffer& buffer, bool key, int64_t pts){");
    RTF_ASSERT(taken != std::string::npos && cb != std::string::npos && taken < cb);
    auto next_cb = src.find("_source.set_video_sample_cb(", cb);
    RTF_ASSERT(next_cb != std::string::npos);
    auto in_cb = [&](const std::string& what) { auto at = src.find(what, cb); return at < next_cb ? at : std::string::npos; };
    auto sinks = in_cb("r_audio_sinks sinks;");
    auto prepare = in_cb("sinks.prepare = [&]() {");
    auto store = in_cb("sinks.store = [&](const r_audio_sample& a) {");
    auto feed = in_cb("sinks.feed = [&](const r_audio_sample& a) {");
    auto restream = in_cb("sinks.restream = [&](const r_audio_sample&) {");
    auto routed = in_cb("route_audio_sample(this->_records_audio, sample, sinks);");
    auto write_context = in_cb("create_write_context(audio_codec_name, audio_codec_parameters, R_STORAGE_MEDIA_TYPE_AUDIO);");
    auto write = in_cb("R_STORAGE_MEDIA_TYPE_AUDIO,\n                    a.data,");
    auto posted = in_cb("this->_sk->post_audio_to_feeds(");
    auto queued = in_cb("this->_sk->iterate_live_restreaming_states(this->_camera.id,");
    for(auto at : {sinks, prepare, store, feed, restream, routed, write_context, write, posted, queued})
        RTF_ASSERT(at != std::string::npos);
    RTF_ASSERT(sinks < prepare && prepare < write_context && write_context < store);
    RTF_ASSERT(store < write && write < feed && feed < posted && posted < restream && restream < queued && queued < routed);
    // Nothing is written, posted or queued before the sinks are set up, and none of it happens a second time outside them.
    RTF_ASSERT(src.find("R_STORAGE_MEDIA_TYPE_AUDIO,", cb) > sinks);
    RTF_ASSERT(in_cb("this->_sk->post_audio_to_feeds(") == posted && src.find("this->_sk->post_audio_to_feeds(", posted + 1) > next_cb);
    // The SDP the session keeps, and the restream mount the video callback makes, leave audio out too.
    RTF_ASSERT(src.find("this->_sdp_medias = recording_sdp_medias(sdp_medias, this->_records_audio);") != std::string::npos);
    auto mount = src.find("recording_audio_encoding(sc.audio_encoding(), this->_records_audio)");
    RTF_ASSERT(mount != std::string::npos && mount > next_cb);
}

void test_r_recording_context::test_route_audio_sample()
{
    // The audio callback's gate (route_audio_sample): with audio off, the sample goes nowhere, not even to prepare;
    // on, it's prepared and goes to storage, the feeds and the restream, in that order, as it came.
    const uint8_t bytes[] = {1, 2, 3, 4, 5};
    r_audio_sample sample {bytes, sizeof(bytes), true, 123456, 789};
    std::vector<std::string> got;
    auto check = [&](const r_audio_sample& a) {
        RTF_ASSERT(a.data == bytes && a.size == sizeof(bytes) && a.key && a.ts == 123456 && a.pts == 789);
    };
    bool prepared_ok = true;
    r_audio_sinks sinks;
    sinks.prepare = [&]() { got.push_back("prepare"); return prepared_ok; };
    sinks.store = [&](const r_audio_sample& a) { check(a); got.push_back("store"); };
    sinks.feed = [&](const r_audio_sample& a) { check(a); got.push_back("feed"); };
    sinks.restream = [&](const r_audio_sample& a) { check(a); got.push_back("restream"); };

    RTF_ASSERT(!route_audio_sample(false, sample, sinks));
    RTF_ASSERT(got.empty());

    RTF_ASSERT(route_audio_sample(true, sample, sinks));
    RTF_ASSERT((got == std::vector<std::string>{"prepare", "store", "feed", "restream"}));

    // A sample prepare can't set up (no write context yet) goes nowhere either.
    got.clear();
    prepared_ok = false;
    RTF_ASSERT(!route_audio_sample(true, sample, sinks));
    RTF_ASSERT((got == std::vector<std::string>{"prepare"}));

    // Off again: nothing, whatever prepare would say.
    got.clear();
    prepared_ok = true;
    RTF_ASSERT(!route_audio_sample(false, sample, sinks));
    RTF_ASSERT(got.empty());
}
