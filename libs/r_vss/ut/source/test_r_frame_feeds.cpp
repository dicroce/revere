#include "test_r_frame_feeds.h"
#include "r_vss/r_frame_feeds.h"
#include <chrono>
#include <thread>
#include <vector>

using namespace r_vss;
using namespace std;
using namespace std::chrono;

REGISTER_TEST_FIXTURE(test_r_frame_feeds);

typedef r_frame_feeds F;

static vector<uint8_t> bytes_of(size_t n, uint8_t v)
{
    return vector<uint8_t>(n, v);
}

static void video(F& f, const string& cam, size_t n, bool key, int64_t ts, const string& params = "sps1", const string& stream = "main")
{
    auto d = bytes_of(n, (uint8_t)ts);
    f.post_video(cam, stream, d.data(), d.size(), key, ts, "h264", params);
}

static void audio(F& f, const string& cam, size_t n, int64_t ts, const string& params = "aac1")
{
    auto d = bytes_of(n, (uint8_t)ts);
    f.post_audio(cam, d.data(), d.size(), ts, "aac", params);
}

// Every frame queued now, without waiting.
static vector<r_feed_frame> drain(F& f, uint64_t feed)
{
    vector<r_feed_frame> out;
    r_feed_frame fr;
    while(f.next(feed, 0, fr) == 1)
        out.push_back(fr);
    return out;
}

void test_r_frame_feeds::test_nothing_subscribed_costs_nothing()
{
    // A camera's recording posts every frame: with no feed, that's one atomic load.
    F f;
    for(int i = 0; i < 100; ++i)
    {
        video(f, "a", 100, i % 10 == 0, i);
        audio(f, "a", 10, i);
    }
    RTF_ASSERT_EQUAL(f.posts_seen(), (size_t)0);
    RTF_ASSERT_EQUAL(f.feeds(), (size_t)0);

    // A feed on another camera makes posts look, not queue.
    auto feed = f.subscribe("b", "", F::VIDEO, 0);
    video(f, "a", 100, true, 1);
    RTF_ASSERT_EQUAL(f.posts_seen(), (size_t)1);
    RTF_ASSERT(drain(f, feed).empty());
    f.unsubscribe(feed);
    video(f, "a", 100, true, 2);
    RTF_ASSERT_EQUAL(f.posts_seen(), (size_t)1);
}

void test_r_frame_feeds::test_video_starts_at_a_key_frame()
{
    F f;
    auto feed = f.subscribe("a", "main", F::VIDEO, 0);
    video(f, "a", 10, false, 1);
    video(f, "a", 10, false, 2);
    video(f, "a", 10, true, 3);
    video(f, "a", 10, false, 4);
    auto got = drain(f, feed);
    RTF_ASSERT_EQUAL(got.size(), (size_t)2);
    RTF_ASSERT_EQUAL(got[0].ts, (int64_t)3);
    RTF_ASSERT_EQUAL(got[0].flags, (uint32_t)(F::VIDEO | F::KEY | F::CODEC_CHANGED));
    RTF_ASSERT_EQUAL(got[1].ts, (int64_t)4);
    RTF_ASSERT_EQUAL(got[1].flags, (uint32_t)F::VIDEO);
    RTF_ASSERT(got[0].codec_name == "h264" && got[0].codec_params == "sps1" && got[0].stream == "main");
    RTF_ASSERT_EQUAL(got[0].data.size(), (size_t)10);
}

void test_r_frame_feeds::test_media_chooses_video_audio_or_both()
{
    F f;
    auto v = f.subscribe("a", "", F::VIDEO, 0);
    auto a = f.subscribe("a", "", F::AUDIO, 0);
    auto both = f.subscribe("a", "", F::VIDEO | F::AUDIO, 0);
    video(f, "a", 10, true, 1);
    audio(f, "a", 5, 2);
    video(f, "a", 10, false, 3);
    audio(f, "a", 5, 4);

    auto gv = drain(f, v), ga = drain(f, a), gb = drain(f, both);
    RTF_ASSERT_EQUAL(gv.size(), (size_t)2);
    RTF_ASSERT((gv[0].flags & F::VIDEO) && (gv[1].flags & F::VIDEO));
    RTF_ASSERT_EQUAL(ga.size(), (size_t)2);
    RTF_ASSERT((ga[0].flags & F::AUDIO) && ga[0].ts == 2 && ga[1].ts == 4);
    RTF_ASSERT(ga[0].stream == "main" && ga[0].codec_name == "aac");
    // In the order they came.
    RTF_ASSERT_EQUAL(gb.size(), (size_t)4);
    for(int i = 0; i < 4; ++i)
        RTF_ASSERT_EQUAL(gb[i].ts, (int64_t)(i + 1));

    // Only VIDEO and AUDIO mean anything, and a feed asks for at least one.
    RTF_ASSERT_EQUAL(f.subscribe("a", "", 0, 0), (uint64_t)0);
    RTF_ASSERT_EQUAL(f.subscribe("a", "", F::KEY, 0), (uint64_t)0);
    RTF_ASSERT_EQUAL(f.subscribe("a", "", F::VIDEO | 1u << 8, 0), (uint64_t)0);
}

void test_r_frame_feeds::test_audio_while_video_waits_for_a_key_frame()
{
    F f;
    auto feed = f.subscribe("a", "", F::VIDEO | F::AUDIO, 0);
    video(f, "a", 10, false, 1);
    audio(f, "a", 5, 2);
    video(f, "a", 10, false, 3);
    audio(f, "a", 5, 4);
    video(f, "a", 10, true, 5);
    auto got = drain(f, feed);
    RTF_ASSERT_EQUAL(got.size(), (size_t)3);
    RTF_ASSERT(got[0].ts == 2 && (got[0].flags & F::AUDIO));
    RTF_ASSERT(got[1].ts == 4 && (got[1].flags & F::AUDIO));
    RTF_ASSERT(got[2].ts == 5 && (got[2].flags & F::KEY));
}

void test_r_frame_feeds::test_a_full_queue_starts_again_with_a_gap()
{
    // A reader that falls behind loses what's queued: video starts again at its next
    // key frame, audio at once, the first of each marked GAP. The queue never holds
    // more than its bound.
    F f;
    auto feed = f.subscribe("a", "", F::VIDEO | F::AUDIO, 1000);
    video(f, "a", 300, true, 1);
    audio(f, "a", 100, 2);
    video(f, "a", 300, false, 3);
    RTF_ASSERT_EQUAL(f.queued_bytes(feed), (size_t)700);
    video(f, "a", 300, false, 4);       // 1000: still fits
    RTF_ASSERT_EQUAL(f.queued_bytes(feed), (size_t)1000);
    video(f, "a", 300, false, 5);       // doesn't: dropped, and so is everything queued
    RTF_ASSERT_EQUAL(f.queued_bytes(feed), (size_t)0);
    audio(f, "a", 100, 6);              // audio carries on
    video(f, "a", 300, false, 7);       // video waits for a key frame
    video(f, "a", 300, true, 8);
    video(f, "a", 300, false, 9);
    RTF_ASSERT(f.queued_bytes(feed) <= 1000);

    auto got = drain(f, feed);
    RTF_ASSERT_EQUAL(got.size(), (size_t)3);
    RTF_ASSERT_EQUAL(got[0].ts, (int64_t)6);
    RTF_ASSERT_EQUAL(got[0].flags, (uint32_t)(F::AUDIO | F::GAP));
    RTF_ASSERT_EQUAL(got[1].ts, (int64_t)8);
    RTF_ASSERT_EQUAL(got[1].flags, (uint32_t)(F::VIDEO | F::KEY | F::GAP));
    RTF_ASSERT_EQUAL(got[2].flags, (uint32_t)F::VIDEO);
}

void test_r_frame_feeds::test_a_frame_bigger_than_the_bound()
{
    // Can never fit: dropped, with the rest, and video waits for a key frame that does.
    F f;
    auto feed = f.subscribe("a", "", F::VIDEO, 500);
    video(f, "a", 100, true, 1);
    video(f, "a", 600, true, 2);
    RTF_ASSERT_EQUAL(f.queued_bytes(feed), (size_t)0);
    video(f, "a", 100, false, 3);
    video(f, "a", 100, true, 4);
    auto got = drain(f, feed);
    RTF_ASSERT_EQUAL(got.size(), (size_t)1);
    RTF_ASSERT_EQUAL(got[0].ts, (int64_t)4);
    RTF_ASSERT(got[0].flags & F::GAP);
}

void test_r_frame_feeds::test_codec_changes_are_marked()
{
    F f;
    auto feed = f.subscribe("a", "", F::VIDEO | F::AUDIO, 0);
    video(f, "a", 10, true, 1, "sps1");
    audio(f, "a", 5, 2, "aac1");
    video(f, "a", 10, false, 3, "sps1");
    audio(f, "a", 5, 4, "aac1");
    video(f, "a", 10, true, 5, "sps2");   // the camera changed its resolution
    audio(f, "a", 5, 6, "aac2");
    video(f, "a", 10, false, 7, "sps2");
    auto got = drain(f, feed);
    RTF_ASSERT_EQUAL(got.size(), (size_t)7);
    vector<bool> changed;
    for(auto& g : got)
        changed.push_back((g.flags & F::CODEC_CHANGED) != 0);
    RTF_ASSERT((changed == vector<bool>{true, true, false, false, true, true, false}));
}

void test_r_frame_feeds::test_streams()
{
    // Empty and "main" are the main stream. "sub" is served from main while there's no
    // substream, its frames saying "main", and its audio is main's. Anything else is refused.
    F f;
    auto m = f.subscribe("a", "", F::VIDEO, 0);
    auto m2 = f.subscribe("a", "main", F::VIDEO, 0);
    auto s = f.subscribe("a", "sub", F::VIDEO | F::AUDIO, 0);
    RTF_ASSERT(m != 0 && m2 != 0 && s != 0);
    RTF_ASSERT_EQUAL(f.subscribe("a", "Sub", F::VIDEO, 0), (uint64_t)0);
    RTF_ASSERT_EQUAL(f.subscribe("a", "secondary", F::VIDEO, 0), (uint64_t)0);

    video(f, "a", 10, true, 1);
    audio(f, "a", 5, 2);
    // A frame from a substream no feed is on yet goes nowhere.
    video(f, "a", 10, true, 3, "sps1", "sub");

    RTF_ASSERT_EQUAL(drain(f, m).size(), (size_t)1);
    RTF_ASSERT_EQUAL(drain(f, m2).size(), (size_t)1);
    auto got = drain(f, s);
    RTF_ASSERT_EQUAL(got.size(), (size_t)2);
    RTF_ASSERT(got[0].stream == "main" && (got[0].flags & F::VIDEO));
    RTF_ASSERT(got[1].stream == "main" && (got[1].flags & F::AUDIO));
}

void test_r_frame_feeds::test_unsubscribe_wakes_a_waiting_reader()
{
    F f;
    auto feed = f.subscribe("a", "", F::VIDEO, 0);
    int result = 99;
    auto started = steady_clock::now();
    thread reader([&]() {
        r_feed_frame fr;
        result = f.next(feed, 10000, fr);
    });
    this_thread::sleep_for(milliseconds(50));
    f.unsubscribe(feed);
    reader.join();
    RTF_ASSERT_EQUAL(result, -1);
    RTF_ASSERT(steady_clock::now() - started < seconds(5));
    RTF_ASSERT_EQUAL(f.feeds(), (size_t)0);
    r_feed_frame fr;
    RTF_ASSERT_EQUAL(f.next(feed, 0, fr), -1);
}

void test_r_frame_feeds::test_closing_a_camera_and_all()
{
    F f;
    auto a = f.subscribe("a", "", F::VIDEO, 0);
    auto b = f.subscribe("b", "", F::VIDEO, 0);
    video(f, "a", 10, true, 1);
    video(f, "b", 10, true, 1);

    // The camera went: its feed is closed (what was queued goes too), the other carries on.
    f.close_camera("a");
    r_feed_frame fr;
    RTF_ASSERT_EQUAL(f.next(a, 0, fr), -1);
    RTF_ASSERT_EQUAL(f.next(b, 0, fr), 1);
    video(f, "a", 10, true, 2);
    RTF_ASSERT_EQUAL(f.next(a, 0, fr), -1);
    RTF_ASSERT_EQUAL(f.feeds(), (size_t)2);   // until it's unsubscribed
    f.unsubscribe(a);
    RTF_ASSERT_EQUAL(f.feeds(), (size_t)1);

    // Everything closes (a plugin stopping).
    int result = 99;
    thread reader([&]() {
        r_feed_frame x;
        result = f.next(b, 10000, x);
    });
    this_thread::sleep_for(milliseconds(50));
    f.close_all();
    reader.join();
    RTF_ASSERT_EQUAL(result, -1);
}

void test_r_frame_feeds::test_a_wait_times_out()
{
    F f;
    auto feed = f.subscribe("a", "", F::VIDEO, 0);
    r_feed_frame fr;
    auto started = steady_clock::now();
    RTF_ASSERT_EQUAL(f.next(feed, 100, fr), 0);
    auto took = steady_clock::now() - started;
    RTF_ASSERT(took >= milliseconds(90) && took < seconds(5));
    RTF_ASSERT_EQUAL(f.next(12345, 0, fr), -1);   // no such feed
}

void test_r_frame_feeds::test_two_feeds_get_their_own_copies()
{
    F f;
    auto one = f.subscribe("a", "", F::VIDEO, 0);
    auto two = f.subscribe("a", "", F::VIDEO, 0);
    video(f, "a", 10, true, 1);
    auto g1 = drain(f, one);
    RTF_ASSERT_EQUAL(g1.size(), (size_t)1);
    RTF_ASSERT_EQUAL(f.queued_bytes(two), (size_t)10);   // reading one leaves the other's
    auto g2 = drain(f, two);
    RTF_ASSERT_EQUAL(g2.size(), (size_t)1);
    RTF_ASSERT(g1[0].data == g2[0].data);
}

void test_r_frame_feeds::test_a_restarted_stream_is_a_gap()
{
    // The camera's stream reconnected: its feeds carry on, video from the next key frame,
    // and the first frame of each media after it marked GAP. Another camera's isn't.
    F f;
    auto feed = f.subscribe("a", "", F::VIDEO | F::AUDIO, 0);
    auto other = f.subscribe("b", "", F::VIDEO, 0);
    video(f, "a", 10, true, 1);
    audio(f, "a", 5, 2);
    video(f, "b", 10, true, 1);
    drain(f, feed);
    drain(f, other);

    f.camera_restarted("a");
    video(f, "a", 10, false, 3);
    audio(f, "a", 5, 4);
    video(f, "a", 10, true, 5);
    video(f, "b", 10, false, 2);
    auto got = drain(f, feed);
    RTF_ASSERT_EQUAL(got.size(), (size_t)2);
    RTF_ASSERT_EQUAL(got[0].flags, (uint32_t)(F::AUDIO | F::GAP));
    RTF_ASSERT_EQUAL(got[1].flags, (uint32_t)(F::VIDEO | F::KEY | F::GAP));
    auto b = drain(f, other);
    RTF_ASSERT_EQUAL(b.size(), (size_t)1);
    RTF_ASSERT_EQUAL(b[0].flags, (uint32_t)F::VIDEO);
}

// A GOP as a recording context passes it: frames (size, key, stored ts), the last one the
// frame being posted. The bytes live in `store`, which outlives the post.
struct gop_of
{
    vector<vector<uint8_t>> store;
    vector<r_gop_frame> frames;
    gop_of& add(size_t n, bool key, int64_t ts, bool stored = true)
    {
        store.push_back(bytes_of(n, (uint8_t)ts));
        frames.push_back({nullptr, n, key, ts, stored});
        for(size_t i = 0; i < frames.size(); ++i)
            frames[i].data = store[i].data();
        return *this;
    }
};

// Posts the GOP's last frame with the GOP, as the recording context does.
static void video_with_gop(F& f, const string& cam, gop_of& g, const string& stream = "main")
{
    auto& last = g.frames.back();
    f.post_video(cam, stream, last.data, last.size, last.key, last.ts, "h264", "sps1", &g.frames);
}

void test_r_frame_feeds::test_a_feed_starts_at_the_newest_key_frame()
{
    F f;
    auto id = f.subscribe("cam1", "", F::VIDEO, 0, true);
    RTF_ASSERT(id != 0);
    RTF_ASSERT(f.gop_wanted());
    // Mid-GOP: the key frame at 10 and the two after it, the last the frame being posted.
    gop_of g;
    g.add(100, true, 10).add(50, false, 11).add(40, false, 12);
    video_with_gop(f, "cam1", g);
    RTF_ASSERT(!f.gop_wanted());
    video(f, "cam1", 30, false, 13);
    auto got = drain(f, id);
    RTF_ASSERT_EQUAL(got.size(), (size_t)4);
    RTF_ASSERT(got[0].ts == 10 && (got[0].flags & F::KEY) && (got[0].flags & F::CODEC_CHANGED) && !(got[0].flags & F::GAP));
    RTF_ASSERT(got[1].ts == 11 && got[1].data.size() == 50 && got[1].data[0] == 11);
    // The frame being posted comes once, as the GOP's last, not again as itself.
    RTF_ASSERT(got[2].ts == 12 && got[3].ts == 13);
    // The stored times, as each frame came: not re-stamped.
    RTF_ASSERT(got[0].stream == "main" && got[3].stream == "main");

    // A "sub" feed served from main starts from main's GOP: by the stream it's serving.
    auto sub = f.subscribe("cam1", "sub", F::VIDEO, 0, true);
    gop_of g2;
    g2.add(100, true, 20).add(50, false, 21);
    video_with_gop(f, "cam1", g2);
    auto s = drain(f, sub);
    RTF_ASSERT(s.size() == 2 && s[0].ts == 20 && s[1].ts == 21 && s[0].stream == "main");
}

void test_r_frame_feeds::test_a_gop_that_cant_start_a_feed()
{
    // Each starts at the next key frame instead, and nothing of the GOP goes out.
    auto starts_at_next_key = [](size_t bound, gop_of g) {
        F f;
        auto id = f.subscribe("cam1", "", F::VIDEO, bound, true);
        video_with_gop(f, "cam1", g);
        RTF_ASSERT(!f.gop_wanted());
        RTF_ASSERT(drain(f, id).empty());
        video(f, "cam1", 30, false, 98);
        video(f, "cam1", 60, true, 99);
        auto got = drain(f, id);
        RTF_ASSERT(got.size() == 1 && got[0].ts == 99 && (got[0].flags & F::KEY));
        // Nothing of the GOP went out, so nothing was dropped: the key frame isn't a gap.
        RTF_ASSERT(!(got[0].flags & F::GAP));
    };
    // Bigger than the bound (the whole GOP is checked before any of it is copied).
    starts_at_next_key(200, gop_of().add(150, true, 10).add(100, false, 11));
    // A frame whose write threw has no stored time.
    starts_at_next_key(0, gop_of().add(100, true, 10).add(50, false, 11, false).add(40, false, 12));
    // Not from a key frame: a GOP longer than the cache.
    starts_at_next_key(0, gop_of().add(50, false, 10).add(40, false, 11));
}

void test_r_frame_feeds::test_a_post_without_a_gop_keeps_the_feed_waiting()
{
    // A frame posted without a GOP (the poster looked before the feed asked) leaves it
    // waiting; the next post that carries one starts it.
    F f;
    auto id = f.subscribe("cam1", "", F::VIDEO, 0, true);
    video(f, "cam1", 30, false, 9);
    RTF_ASSERT(f.gop_wanted());
    RTF_ASSERT(drain(f, id).empty());
    gop_of g;
    g.add(100, true, 10).add(50, false, 11);
    video_with_gop(f, "cam1", g);
    auto got = drain(f, id);
    RTF_ASSERT(got.size() == 2 && got[0].ts == 10 && got[1].ts == 11);
}

void test_r_frame_feeds::test_feeds_that_didnt_ask_ignore_the_gop()
{
    F f;
    auto plain = f.subscribe("cam1", "", F::VIDEO, 0);
    auto wants = f.subscribe("cam1", "", F::VIDEO, 0, true);
    // An audio-only feed has no video to start: it never waits for a GOP, so posters
    // don't build one for it.
    {
        F only_audio;
        RTF_ASSERT(only_audio.subscribe("cam1", "", F::AUDIO, 0, true) != 0);
        RTF_ASSERT(!only_audio.gop_wanted());
    }
    auto aud = f.subscribe("cam1", "", F::AUDIO, 0, true);
    RTF_ASSERT(aud != 0);
    gop_of g;
    g.add(100, true, 10).add(50, false, 11);
    video_with_gop(f, "cam1", g);
    // Not a key frame, so the plain feed takes nothing yet; the one that asked has the GOP.
    RTF_ASSERT(drain(f, plain).empty());
    RTF_ASSERT_EQUAL(drain(f, wants).size(), (size_t)2);
    // Another camera's GOP touches neither.
    auto other = f.subscribe("cam2", "", F::VIDEO, 0, true);
    video_with_gop(f, "cam1", g);
    RTF_ASSERT(drain(f, other).empty());
    RTF_ASSERT(f.gop_wanted());
}

void test_r_frame_feeds::test_gop_wanted_counts_waiting_feeds()
{
    F f;
    RTF_ASSERT(!f.gop_wanted());
    auto a = f.subscribe("cam1", "", F::VIDEO, 0, true);
    auto b = f.subscribe("cam2", "", F::VIDEO | F::AUDIO, 0, true);
    f.unsubscribe(a);
    RTF_ASSERT(f.gop_wanted());
    f.close_camera("cam2");
    RTF_ASSERT(!f.gop_wanted());
    f.unsubscribe(b);
    RTF_ASSERT(!f.gop_wanted());
    f.subscribe("cam3", "", F::VIDEO, 0, true);
    f.subscribe("cam4", "", F::VIDEO, 0, true);
    f.close_all();
    RTF_ASSERT(!f.gop_wanted());
    // Once a waiting feed starts, it no longer counts.
    auto c = f.subscribe("cam5", "", F::VIDEO, 0, true);
    gop_of g;
    g.add(10, true, 1);
    video_with_gop(f, "cam5", g);
    RTF_ASSERT(!f.gop_wanted());
    f.unsubscribe(c);
    RTF_ASSERT(!f.gop_wanted());
}
