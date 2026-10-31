#ifndef __revere_frame_tap_h
#define __revere_frame_tap_h

#include "r_utils/r_macro.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace r_vss
{

// The newest picture of one camera while something watches it closely (PTZ
// view: pictures that show a move as it happens). A key frame is only about one
// a second, so the tap decodes every frame the camera sends from the moment it
// starts, and keeps the newest one, scaled so its longer edge is long_edge, as
// YUV. A JPEG is encoded only when one is asked for, once per frame.
//
// Frames are posted from the camera's recording callback and decoded on the
// tap's own thread (software decode: see _decode_single_frame in r_query.cpp
// for why not hardware). The queue is bounded: a decoder that falls behind
// drops what's queued and starts again at the next key frame, so the picture
// stays current rather than catching up on old ones.
class r_frame_tap final
{
public:
    // The codec's extradata (SPS/PPS) from the stream's codec name and parameters.
    using extradata_fn = std::function<std::vector<uint8_t>(const std::string& codec_name, const std::string& codec_params)>;

    R_API explicit r_frame_tap(uint16_t long_edge, extradata_fn extradata = extradata_fn());
    R_API r_frame_tap(const r_frame_tap&) = delete;
    R_API r_frame_tap& operator=(const r_frame_tap&) = delete;
    R_API ~r_frame_tap() noexcept;

    // Ends the decoder thread and joins it; after it, post() does nothing and the
    // destructor has nothing to wait for. r_frame_taps stops a tap it reaps on
    // the reaping thread, so whichever thread lets the last copy go (a camera's
    // recording thread in post(), say) never waits on a decode.
    R_API void stop() noexcept;

    // One frame as the camera sent it; ts is its stored time (epoch ms).
    R_API void post(const uint8_t* data, size_t size, bool key, int64_t ts, const std::string& codec_name, const std::string& codec_params);

    // The newest picture as a JPEG if it's newer than `after` (epoch ms); false
    // when there's none yet or nothing newer. A different long_edge takes
    // effect from the next picture.
    R_API bool newest_jpeg(int64_t after, uint16_t long_edge, std::vector<uint8_t>& jpeg, int64_t& ts);

    // For tests and logs.
    R_API size_t frames_decoded() const;
    R_API size_t pictures_made() const;
    R_API size_t queue_resets() const;

    // Frames waiting at most; past it the queue is dropped (see above).
    static constexpr size_t MAX_QUEUED = 120;
    // A picture is made from a decoded frame at most this often.
    static constexpr int64_t PICTURE_EVERY_MS = 100;

private:
    struct _frame
    {
        std::vector<uint8_t> data;
        bool key {false};
        int64_t ts {0};
        std::string codec_name;
        std::string codec_params;
    };

    struct _picture
    {
        int64_t ts {0};
        uint16_t w {0};
        uint16_t h {0};
        std::shared_ptr<std::vector<uint8_t>> yuv;
    };

    void _entry_point();

    extradata_fn _extradata;
    std::atomic<uint16_t> _long_edge;

    mutable std::mutex _q_lock;
    std::condition_variable _q_cond;
    std::deque<_frame> _q;
    bool _need_key {true};
    bool _running {true};

    mutable std::mutex _picture_lock;
    _picture _newest;
    // The last JPEG made, and the frame it's of.
    int64_t _jpeg_ts {-1};
    std::vector<uint8_t> _jpeg;

    std::atomic<size_t> _frames_decoded {0};
    std::atomic<size_t> _pictures_made {0};
    std::atomic<size_t> _queue_resets {0};

    std::thread _th;
};

enum class r_frame_tap_status
{
    picture,        // jpeg holds a newer picture
    nothing_newer,  // the newest is the one the caller has (or none has been decoded yet)
    too_many        // MAX_TAPS cameras are being watched already
};

struct r_frame_tap_result
{
    r_frame_tap_status status {r_frame_tap_status::nothing_newer};
    int64_t ts {0};
    std::vector<uint8_t> jpeg;
};

// The cameras being watched closely, one r_frame_tap each. A camera's tap starts
// with the first ask for its picture and stops IDLE_MS after the last one; at
// most MAX_TAPS run at once. post() is called for every frame of every camera,
// so it costs one atomic load while no camera is tapped.
class r_frame_taps final
{
public:
    using clock_fn = std::function<std::chrono::steady_clock::time_point()>;

    R_API explicit r_frame_taps(r_frame_tap::extradata_fn extradata = r_frame_tap::extradata_fn(), clock_fn now = clock_fn());
    R_API r_frame_taps(const r_frame_taps&) = delete;
    R_API r_frame_taps& operator=(const r_frame_taps&) = delete;

    R_API void post(const std::string& camera_id, const uint8_t* data, size_t size, bool key, int64_t ts, const std::string& codec_name, const std::string& codec_params);

    // The camera's newest picture if newer than `after`, starting its tap if
    // none runs. The caller checks the camera exists and is recording.
    R_API r_frame_tap_result newest(const std::string& camera_id, uint16_t long_edge, int64_t after);

    // Stops taps not asked for in IDLE_MS, joining their threads. Called by
    // newest() and by the keeper's loop, never by post(): that runs on a
    // camera's recording thread, which mustn't wait on another tap's decoder.
    R_API void reap();

    R_API size_t active() const;

    // The camera's tap, or null: for tests.
    R_API std::shared_ptr<r_frame_tap> tap(const std::string& camera_id) const;

    static constexpr size_t MAX_TAPS = 4;
    static constexpr int64_t IDLE_MS = 5000;

private:
    struct _entry
    {
        std::shared_ptr<r_frame_tap> tap;
        std::chrono::steady_clock::time_point last_asked;
    };

    // Takes the idle taps out of _taps (under _lock), for the caller to stop
    // (_stop_all) and let go after.
    std::vector<std::shared_ptr<r_frame_tap>> _take_idle(std::chrono::steady_clock::time_point now);
    static void _stop_all(std::vector<std::shared_ptr<r_frame_tap>>& taps);

    r_frame_tap::extradata_fn _extradata;
    clock_fn _now;
    mutable std::mutex _lock;
    std::map<std::string, _entry> _taps;
    std::atomic<size_t> _count {0};
};

}

#endif
