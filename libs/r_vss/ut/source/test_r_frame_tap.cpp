#include "test_r_frame_tap.h"
#include "r_vss/r_frame_tap.h"
#include "r_av/r_demuxer.h"
#include "r_pipeline/r_gst_source.h"
#include "r_utils/r_file.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace r_vss;
using namespace r_av;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

REGISTER_TEST_FIXTURE(test_r_frame_tap);

// The test clip; defined once, by bad_guy.h in test_r_motion_engine.cpp.
extern unsigned char bad_guy_mp4[];
extern unsigned int bad_guy_mp4_len;

namespace
{

// The test clip's video frames as a camera's are recorded: Annex B (start codes),
// SPS and PPS before each key frame; ts in ms, ~30 fps. The MP4 holds them
// length-prefixed, with SPS/PPS in its avcC.
struct clip_frame
{
    vector<uint8_t> data;
    bool key;
    int64_t ts;
};

struct clip
{
    vector<uint8_t> extradata;
    vector<clip_frame> frames;
};

const uint8_t START_CODE[] = {0, 0, 0, 1};

// SPS and PPS from an avcC record, each after a start code.
vector<uint8_t> _annexb_parameter_sets(const vector<uint8_t>& avcc)
{
    vector<uint8_t> out;
    if(avcc.size() < 7)
        return out;
    size_t i = 5;
    auto take = [&](size_t count) {
        for(size_t n = 0; n < count && i + 2 <= avcc.size(); ++n)
        {
            size_t len = (size_t)((avcc[i] << 8) | avcc[i + 1]);
            i += 2;
            if(i + len > avcc.size())
                return;
            out.insert(out.end(), START_CODE, START_CODE + 4);
            out.insert(out.end(), avcc.begin() + i, avcc.begin() + i + len);
            i += len;
        }
    };
    take(avcc[i++] & 0x1f);
    if(i < avcc.size())
        take(avcc[i++]);
    return out;
}

// Length-prefixed NAL units (4-byte lengths) to start codes.
vector<uint8_t> _annexb(const uint8_t* data, size_t size)
{
    vector<uint8_t> out;
    size_t i = 0;
    while(i + 4 <= size)
    {
        size_t len = ((size_t)data[i] << 24) | ((size_t)data[i + 1] << 16) | ((size_t)data[i + 2] << 8) | data[i + 3];
        i += 4;
        if(i + len > size)
            break;
        out.insert(out.end(), START_CODE, START_CODE + 4);
        out.insert(out.end(), data + i, data + i + len);
        i += len;
    }
    return out;
}

const clip& _clip()
{
    static clip c = []() {
        clip out;
        {
            r_demuxer demuxer("frame_tap_bad_guy.mp4", false);
            auto vsi = demuxer.get_video_stream_index();
            auto parameter_sets = _annexb_parameter_sets(demuxer.get_extradata(vsi));
            int64_t ts = 1000;
            while(demuxer.read_frame())
            {
                auto fi = demuxer.get_frame_info();
                if(fi.index != vsi)
                    continue;
                vector<uint8_t> frame;
                if(fi.key)
                    frame = parameter_sets;
                auto nals = _annexb(fi.data, fi.size);
                frame.insert(frame.end(), nals.begin(), nals.end());
                out.frames.push_back({frame, fi.key, ts});
                ts += 33;
            }
        }
        return out;
    }();
    return c;
}

// Frames the clip's decoder holds back to put B-frames in order (its has_b_frames).
const size_t CLIP_REORDER = 2;

// SPS/PPS come inline, as with most cameras: no extradata.
r_frame_tap::extradata_fn _clip_extradata()
{
    return [](const string&, const string&) { return vector<uint8_t>(); };
}

void _post(r_frame_tap& tap, const clip_frame& f)
{
    tap.post(f.data.data(), f.data.size(), f.key, f.ts, "h264", "");
}

// Waits up to ~5 s for pred.
template<typename P>
bool _eventually(P pred)
{
    for(int i = 0; i < 500; ++i)
    {
        if(pred())
            return true;
        this_thread::sleep_for(milliseconds(10));
    }
    return pred();
}

// The JPEG's width and height, from its SOF marker; false if it isn't a JPEG.
bool _jpeg_size(const vector<uint8_t>& j, uint16_t& w, uint16_t& h)
{
    if(j.size() < 4 || j[0] != 0xFF || j[1] != 0xD8)
        return false;
    size_t i = 2;
    while(i + 9 < j.size())
    {
        if(j[i] != 0xFF)
            return false;
        auto marker = j[i + 1];
        auto len = (size_t)((j[i + 2] << 8) | j[i + 3]);
        if(marker >= 0xC0 && marker <= 0xC3)
        {
            h = (uint16_t)((j[i + 5] << 8) | j[i + 6]);
            w = (uint16_t)((j[i + 7] << 8) | j[i + 8]);
            return true;
        }
        i += 2 + len;
    }
    return false;
}

}

void test_r_frame_tap::setup()
{
    r_pipeline::gstreamer_init();
    r_fs::write_file(bad_guy_mp4, bad_guy_mp4_len, "frame_tap_bad_guy.mp4");
    (void)_clip();
}

void test_r_frame_tap::teardown()
{
}

// Every frame is decoded, not only key frames, and the picture kept is the newest.
void test_r_frame_tap::test_decodes_every_frame_and_keeps_the_newest()
{
    auto& c = _clip();
    RTF_ASSERT(c.frames.size() > 10);
    size_t keys = 0;
    for(auto& f : c.frames)
        keys += f.key ? 1 : 0;
    RTF_ASSERT(keys < c.frames.size());   // the clip has frames between key frames

    // Two seconds of the clip from its first key frame, fewer than the queue holds.
    RTF_ASSERT(c.frames.front().key);
    const size_t n = 60;
    r_frame_tap tap(320, _clip_extradata());
    for(size_t i = 0; i < n; ++i)
        _post(tap, c.frames[i]);
    // The clip has B-frames (two to reorder), so the last two come out with later ones.
    RTF_ASSERT(_eventually([&]() { return tap.frames_decoded() == n - CLIP_REORDER; }));
    RTF_ASSERT_EQUAL(tap.queue_resets(), (size_t)0);
    // Every frame decoded, but a picture made at most every PICTURE_EVERY_MS: a
    // burst of them makes only a few.
    RTF_ASSERT(tap.pictures_made() >= 1);
    RTF_ASSERT(tap.pictures_made() <= 5);

    // The next frame arrives after a picture's interval: it's the one kept.
    this_thread::sleep_for(milliseconds(r_frame_tap::PICTURE_EVERY_MS + 20));
    _post(tap, c.frames[n]);
    vector<uint8_t> jpeg;
    int64_t ts = 0;
    RTF_ASSERT(_eventually([&]() { return tap.newest_jpeg(0, 320, jpeg, ts) && ts == c.frames[n].ts; }));
    uint16_t w = 0, h = 0;
    RTF_ASSERT(_jpeg_size(jpeg, w, h));
    // The clip is 96x54: a picture is never scaled up.
    RTF_ASSERT_EQUAL(w, (uint16_t)96);
    RTF_ASSERT_EQUAL(h, (uint16_t)54);

    // Nothing newer than the picture the caller has.
    vector<uint8_t> again;
    int64_t again_ts = 0;
    RTF_ASSERT(!tap.newest_jpeg(ts, 320, again, again_ts));
}

void test_r_frame_tap::test_picture_is_scaled_to_the_long_edge()
{
    auto& c = _clip();
    r_frame_tap tap(48, _clip_extradata());
    for(size_t i = 0; i < 30; ++i)
        _post(tap, c.frames[i]);
    vector<uint8_t> jpeg;
    int64_t ts = 0;
    RTF_ASSERT(_eventually([&]() { return tap.newest_jpeg(0, 48, jpeg, ts); }));
    uint16_t w = 0, h = 0;
    RTF_ASSERT(_jpeg_size(jpeg, w, h));
    // 96x54 to a long edge of 48: 48x27, made even.
    RTF_ASSERT_EQUAL(w, (uint16_t)48);
    RTF_ASSERT_EQUAL(h, (uint16_t)26);
}

// Frames before the first key frame can't be decoded, so they aren't kept.
void test_r_frame_tap::test_waits_for_a_key_frame()
{
    auto& c = _clip();
    r_frame_tap tap(320, _clip_extradata());
    for(auto& f : c.frames)
        if(!f.key)
            _post(tap, f);
    this_thread::sleep_for(milliseconds(300));
    RTF_ASSERT_EQUAL(tap.frames_decoded(), (size_t)0);
    vector<uint8_t> jpeg;
    int64_t ts = 0;
    RTF_ASSERT(!tap.newest_jpeg(0, 320, jpeg, ts));
}

// A decoder that falls behind drops the queue and starts again at the next key frame.
void test_r_frame_tap::test_a_full_queue_starts_again_at_a_key_frame()
{
    auto& c = _clip();
    mutex m;
    condition_variable cv;
    bool release = false;
    // The decoder is made on the tap's thread at the first key frame: holding it
    // there lets the queue fill.
    auto let_go = [&]() {
        {
            lock_guard<mutex> g(m);
            release = true;
        }
        cv.notify_all();
    };
    r_frame_tap tap(320, [&](const string&, const string&) {
        unique_lock<mutex> lk(m);
        cv.wait(lk, [&]() { return release; });
        return vector<uint8_t>();
    });
    // Declared after the tap, so it lets the decoder go before the tap's
    // destructor joins it, even when an assertion fails first.
    struct on_exit { function<void()> f; ~on_exit() { f(); } } releaser{let_go};

    size_t first_key = 0;
    while(!c.frames[first_key].key)
        ++first_key;
    _post(tap, c.frames[first_key]);
    this_thread::sleep_for(milliseconds(50));
    // Non-key frames past the limit.
    size_t posted = 0;
    for(size_t i = 0; posted <= r_frame_tap::MAX_QUEUED; i = (i + 1) % c.frames.size())
    {
        if(c.frames[i].key)
            continue;
        _post(tap, c.frames[i]);
        ++posted;
    }
    RTF_ASSERT_EQUAL(tap.queue_resets(), (size_t)1);

    let_go();
    // Whatever it was holding is decoded meanwhile.
    this_thread::sleep_for(milliseconds(200));
    auto decoded = tap.frames_decoded();

    // After the reset, non-key frames are dropped until a key frame: none decoded.
    for(size_t i = 0; i < c.frames.size() && i < 40; ++i)
        if(!c.frames[i].key)
            _post(tap, c.frames[i]);
    this_thread::sleep_for(milliseconds(300));
    RTF_ASSERT_EQUAL(tap.frames_decoded(), decoded);
    // From a key frame (and the frames its B-frames need), decoding goes on.
    for(size_t i = first_key; i <= first_key + CLIP_REORDER; ++i)
        _post(tap, c.frames[i]);
    RTF_ASSERT(_eventually([&]() { return tap.frames_decoded() > decoded; }));
}

void test_r_frame_tap::test_a_picture_is_encoded_once()
{
    auto& c = _clip();
    r_frame_tap tap(320, _clip_extradata());
    // A key frame and the frames its B-frames need: one picture.
    for(size_t i = 0; i <= CLIP_REORDER; ++i)
        _post(tap, c.frames[i]);
    vector<uint8_t> a, b;
    int64_t ta = 0, tb = 0;
    RTF_ASSERT(_eventually([&]() { return tap.newest_jpeg(0, 320, a, ta); }));
    RTF_ASSERT(tap.newest_jpeg(0, 320, b, tb));
    RTF_ASSERT(a == b);
    RTF_ASSERT_EQUAL(ta, tb);
    RTF_ASSERT_EQUAL(tap.pictures_made(), (size_t)1);
}

void test_r_frame_tap::test_taps_start_on_ask_and_only_take_their_camera()
{
    auto& c = _clip();
    r_frame_taps taps(_clip_extradata());
    // No camera asked for: frames are ignored.
    for(auto& f : c.frames)
        taps.post("cam_a", f.data.data(), f.data.size(), f.key, f.ts, "h264", "");
    RTF_ASSERT_EQUAL(taps.active(), (size_t)0);

    // The first ask starts the tap; it has nothing yet.
    auto first = taps.newest("cam_a", 320, 0);
    RTF_ASSERT(first.status == r_frame_tap_status::nothing_newer);
    RTF_ASSERT_EQUAL(taps.active(), (size_t)1);

    for(auto& f : c.frames)
    {
        taps.post("cam_b", f.data.data(), f.data.size(), f.key, f.ts, "h264", "");
        taps.post("cam_a", f.data.data(), f.data.size(), f.key, f.ts, "h264", "");
    }
    r_frame_tap_result got;
    RTF_ASSERT(_eventually([&]() { got = taps.newest("cam_a", 320, 0); return got.status == r_frame_tap_status::picture; }));
    RTF_ASSERT(!got.jpeg.empty());
    RTF_ASSERT(got.ts > 0);
    // cam_b was never asked for: it has no tap.
    RTF_ASSERT_EQUAL(taps.active(), (size_t)1);
}

void test_r_frame_tap::test_at_most_max_taps()
{
    r_frame_taps taps(_clip_extradata());
    for(size_t i = 0; i < r_frame_taps::MAX_TAPS; ++i)
        RTF_ASSERT(taps.newest("cam_" + to_string(i), 320, 0).status == r_frame_tap_status::nothing_newer);
    RTF_ASSERT(taps.newest("one_too_many", 320, 0).status == r_frame_tap_status::too_many);
    // One already tapped is still answered.
    RTF_ASSERT(taps.newest("cam_0", 320, 0).status == r_frame_tap_status::nothing_newer);
    RTF_ASSERT_EQUAL(taps.active(), r_frame_taps::MAX_TAPS);
}

void test_r_frame_tap::test_an_idle_tap_stops()
{
    auto now = steady_clock::now();
    r_frame_taps taps(_clip_extradata(), [&]() { return now; });
    taps.newest("cam_a", 320, 0);
    taps.newest("cam_b", 320, 0);
    now += milliseconds(r_frame_taps::IDLE_MS - 1);
    taps.newest("cam_b", 320, 0);   // asked again: kept
    taps.reap();
    RTF_ASSERT_EQUAL(taps.active(), (size_t)2);
    now += milliseconds(1);
    taps.reap();
    RTF_ASSERT_EQUAL(taps.active(), (size_t)1);
    now += milliseconds(r_frame_taps::IDLE_MS);
    // Frames never reap: they arrive on a camera's recording thread, which
    // mustn't wait on a tap's decoder thread to end. The keeper's loop does.
    auto& f = _clip().frames.front();
    taps.post("cam_b", f.data.data(), f.data.size(), f.key, f.ts, "h264", "");
    taps.post("cam_a", f.data.data(), f.data.size(), f.key, f.ts, "h264", "");
    RTF_ASSERT_EQUAL(taps.active(), (size_t)1);
    taps.reap();
    RTF_ASSERT_EQUAL(taps.active(), (size_t)0);
    // Room again for a new one.
    RTF_ASSERT(taps.newest("cam_c", 320, 0).status == r_frame_tap_status::nothing_newer);
}

// stop() ends the decoder: frames after it are dropped, and the destructor has
// nothing left to wait for.
void test_r_frame_tap::test_stop_ends_decoding()
{
    auto& c = _clip();
    r_frame_tap tap(320, _clip_extradata());
    for(size_t i = 0; i < 10; ++i)
        _post(tap, c.frames[i]);
    RTF_ASSERT(_eventually([&]() { return tap.frames_decoded() >= 10 - CLIP_REORDER; }));
    tap.stop();
    auto decoded = tap.frames_decoded();
    // More than a queue's worth after the stop: none kept (no reset needed), none decoded.
    for(size_t i = 0; i <= r_frame_tap::MAX_QUEUED + 10; ++i)
        _post(tap, c.frames[10 + i % 40]);
    this_thread::sleep_for(milliseconds(200));
    RTF_ASSERT_EQUAL(tap.frames_decoded(), decoded);
    RTF_ASSERT_EQUAL(tap.queue_resets(), (size_t)0);
    tap.stop();   // again: harmless
}

// The reaper stops an idle tap itself, joining its decoder there, even while
// someone else (post() on a recording thread) still holds a copy: letting that
// copy go must never be what waits on a decode.
void test_r_frame_tap::test_the_reaper_joins_not_the_last_holder()
{
    auto& c = _clip();
    mutex m;
    condition_variable cv;
    bool release = false;
    auto let_go = [&]() {
        {
            lock_guard<mutex> g(m);
            release = true;
        }
        cv.notify_all();
    };
    auto now = steady_clock::now();
    // The decoder blocks at its first key frame until let go: a decode in progress.
    r_frame_taps taps([&](const string&, const string&) {
        unique_lock<mutex> lk(m);
        cv.wait(lk, [&]() { return release; });
        return vector<uint8_t>();
    }, [&]() { return now; });
    struct on_exit { function<void()> f; ~on_exit() { f(); } } releaser{let_go};

    taps.newest("cam_a", 320, 0);
    auto& f = c.frames.front();
    taps.post("cam_a", f.data.data(), f.data.size(), f.key, f.ts, "h264", "");
    this_thread::sleep_for(milliseconds(50));

    // post()'s copy, held across the reap.
    auto held = taps.tap("cam_a");
    RTF_ASSERT(held != nullptr);

    now += milliseconds(r_frame_taps::IDLE_MS);
    atomic<bool> reaped {false};
    thread reaper([&]() { taps.reap(); reaped = true; });
    // Joined however the test ends, after the decode is let go.
    struct joiner { thread& t; function<void()> go; ~joiner() { go(); if(t.joinable()) t.join(); } } join_reaper{reaper, let_go};
    // The reaper waits for the decode to end: it's the one joining.
    this_thread::sleep_for(milliseconds(150));
    RTF_ASSERT(!reaped.load());
    let_go();
    reaper.join();
    RTF_ASSERT(reaped.load());
    RTF_ASSERT_EQUAL(taps.active(), (size_t)0);

    // Letting the last copy go waits on nothing.
    auto t0 = steady_clock::now();
    held.reset();
    RTF_ASSERT(steady_clock::now() - t0 < milliseconds(50));
}
