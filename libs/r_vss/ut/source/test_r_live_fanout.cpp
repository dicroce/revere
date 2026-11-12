#include "test_r_live_fanout.h"
#include "r_vss/r_recording_context.h"
#include <gst/gst.h>
#include <memory>
#include <vector>

using namespace r_vss;
using namespace r_pipeline;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_live_fanout);

// The fan-out is the main stream's live restream code, moved out of the recording context as it was so the substream's
// mount can use it. These pin what it does: a viewer is brought up to date from the cached GOP with compressed
// timestamps and then follows the source's rhythm, and nothing is sent before a key frame.

typedef vector<shared_ptr<live_restreaming_state>> viewers_t;

static shared_ptr<live_restreaming_state> _viewer()
{
    return make_shared<live_restreaming_state>();
}

static void _post(r_live_video_fanout& fanout, viewers_t& viewers, int64_t pts, bool key)
{
    gst_init(nullptr, nullptr);      // a frame is a GstBuffer; nothing else in this process has started GStreamer
    uint8_t data[4] = {1, 2, 3, 4};
    r_gst_buffer buffer(data, sizeof(data));
    fanout.post(pts, key, buffer, [&](const function<void(live_restreaming_state&)>& fn) {
        for(auto& v : viewers)
            fn(*v);
    });
}

static vector<_frame_context> _drain(live_restreaming_state& v)
{
    // (poll with a zero wait blocks until something arrives, so it's only called with something queued.)
    vector<_frame_context> out;
    while(v.video_samples.size() > 0)
    {
        auto f = v.video_samples.poll(chrono::milliseconds(1));
        if(f.is_null())
            break;
        out.push_back(f.value());
    }
    return out;
}

void test_r_live_fanout::test_the_cache_starts_at_a_key_frame()
{
    r_live_video_fanout fanout;
    viewers_t none;
    _post(fanout, none, 1000, true);
    _post(fanout, none, 1033, false);
    _post(fanout, none, 1066, false);
    RTF_ASSERT_EQUAL(fanout.cached_frames(), (size_t)3);
    _post(fanout, none, 2000, true);          // a key frame starts the cache over
    RTF_ASSERT_EQUAL(fanout.cached_frames(), (size_t)1);
    _post(fanout, none, 2033, false);
    RTF_ASSERT_EQUAL(fanout.cached_frames(), (size_t)2);
}

void test_r_live_fanout::test_a_new_viewer_gets_the_cached_gop_as_a_burst()
{
    r_live_video_fanout fanout;
    viewers_t viewers;
    _post(fanout, viewers, 1000, true);
    _post(fanout, viewers, 1033, false);
    _post(fanout, viewers, 1066, false);

    auto v = _viewer();
    viewers.push_back(v);
    _post(fanout, viewers, 1100, false);      // the viewer's first frame: the whole GOP, this frame included

    auto burst = _drain(*v);
    RTF_ASSERT_EQUAL(burst.size(), (size_t)4);
    RTF_ASSERT(burst[0].key);
    RTF_ASSERT(!burst[1].key && !burst[2].key && !burst[3].key);
    for(size_t i = 0; i < burst.size(); ++i)
    {
        RTF_ASSERT_EQUAL(burst[i].gst_pts, (uint64_t)(i * 1000000));    // 1 ms apart, not the source's 33 ms
        RTF_ASSERT_EQUAL(burst[i].gst_dts, (uint64_t)(i * 1000000));
    }
    RTF_ASSERT(v->live_restream_key_sent);
    RTF_ASSERT(!v->needs_gop_replay);

    // The next live frame carries on one source interval after the burst's last frame.
    _post(fanout, viewers, 1133, false);
    auto next = _drain(*v);
    RTF_ASSERT_EQUAL(next.size(), (size_t)1);
    RTF_ASSERT_EQUAL(next[0].gst_pts, (uint64_t)(3 * 1000000 + 33 * 1000000));
}

void test_r_live_fanout::test_a_viewer_waits_for_a_key_frame_without_a_cache()
{
    // A cache that doesn't start at a key frame (it was capped past it) can't be replayed: the viewer takes the
    // key-gated path and gets nothing until a key frame.
    r_live_video_fanout fanout;
    viewers_t viewers;
    for(int i = 0; i < (int)GOP_CACHE_MAX_FRAMES + 20; ++i)
        _post(fanout, viewers, 1000 + i * 33, i == 0);
    RTF_ASSERT_EQUAL(fanout.cached_frames(), GOP_CACHE_MAX_FRAMES);

    auto v = _viewer();
    viewers.push_back(v);
    _post(fanout, viewers, 90000, false);
    RTF_ASSERT(_drain(*v).empty());
    RTF_ASSERT(!v->live_restream_key_sent);

    _post(fanout, viewers, 90033, true);
    auto got = _drain(*v);
    RTF_ASSERT_EQUAL(got.size(), (size_t)1);
    RTF_ASSERT(got[0].key);
    RTF_ASSERT_EQUAL(got[0].gst_pts, (uint64_t)0);   // its timeline starts at this key frame
}

void test_r_live_fanout::test_a_viewer_gets_each_frame_after_its_first_key_frame()
{
    r_live_video_fanout fanout;
    viewers_t viewers;
    auto v = _viewer();
    v->needs_gop_replay = false;              // joined between frames with an empty cache: key-gated
    viewers.push_back(v);

    _post(fanout, viewers, 1000, false);
    RTF_ASSERT(_drain(*v).empty());           // not a key frame
    _post(fanout, viewers, 1033, true);
    _post(fanout, viewers, 1066, false);
    _post(fanout, viewers, 1099, false);
    auto got = _drain(*v);
    RTF_ASSERT_EQUAL(got.size(), (size_t)3);
    RTF_ASSERT_EQUAL(got[0].gst_pts, (uint64_t)0);
    RTF_ASSERT_EQUAL(got[1].gst_pts, (uint64_t)(33 * 1000000));
    RTF_ASSERT_EQUAL(got[2].gst_pts, (uint64_t)(66 * 1000000));
}

void test_r_live_fanout::test_the_cache_is_capped()
{
    r_live_video_fanout fanout;
    viewers_t none;
    _post(fanout, none, 0, true);
    for(int i = 1; i < (int)GOP_CACHE_MAX_FRAMES * 2; ++i)
        _post(fanout, none, i * 33, false);
    RTF_ASSERT_EQUAL(fanout.cached_frames(), GOP_CACHE_MAX_FRAMES);
}

void test_r_live_fanout::test_two_viewers_are_independent()
{
    r_live_video_fanout fanout;
    viewers_t viewers;
    _post(fanout, viewers, 1000, true);
    auto a = _viewer();
    viewers.push_back(a);
    _post(fanout, viewers, 1033, false);      // a joins here and is brought up to date
    auto b = _viewer();
    viewers.push_back(b);
    _post(fanout, viewers, 1066, false);      // b joins here

    RTF_ASSERT_EQUAL(_drain(*a).size(), (size_t)3);   // burst of 2, then this frame
    RTF_ASSERT_EQUAL(_drain(*b).size(), (size_t)3);   // burst of 3 (the whole GOP, this frame included)
}
