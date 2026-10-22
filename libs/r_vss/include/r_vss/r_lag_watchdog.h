#ifndef __r_vss_r_lag_watchdog_h
#define __r_vss_r_lag_watchdog_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>

namespace r_vss
{

// One camera's lag, as the stream status and the logs report it.
struct r_lag_snapshot
{
    int64_t lag_ms {0};
    int64_t peak_lag_ms {0};
    int64_t behind_for_ms {0};
    double rate {1.0};
    bool lagging {false};
    bool wants_reconnect {false};
    int episodes {0};
};

// How far one camera's frames reach us behind live, without trusting the
// camera's clock (camera clocks can be wrong, and some jump).
// Over RTSP/TCP nothing is dropped: a link that can't carry the stream
// buffers instead, and the camera silently falls further behind. Events have
// to be near real time (a person at the door → a push with a picture), so
// this watches for it. No threads and no clock: the caller passes each video
// frame's camera time (RTP-derived pts, ms) and the steady clock (ms) when it
// arrived.
//
// The measure: offset = arrival − camera time. A frame can't arrive before
// it was captured, so the lowest offset seen is the stream's live baseline,
// and lag = offset − baseline is how much later than that this frame came.
// A healthy stream advances camera time at 1.0× arrival time and lag stays at
// jitter; a growing backlog advances it slower, and lag is the sum of the
// shortfall. The baseline:
//  - drops at once to any lower offset (a frame that came sooner: the
//    connect-time burst, or a camera clock that jumped forward);
//  - rises towards higher offsets by at most DRIFT_ALLOWANCE_PPM of the time
//    passed (1 ms a second), so a camera clock that runs a little slow never
//    adds up to lag: a crystal's 50-100 ppm, an NTP slew's at most 500. A
//    backlog growing faster than that shows (0.4% short: 3 ms a second);
//    a link 1% short of the stream's bitrate adds 9, and a struggling one
//    hundreds;
//  - is re-anchored at the frame when camera time steps back more than
//    JUMP_MS from the newest camera time seen (a clock jump, a stream
//    restart). Smaller steps back are B-frame order. Lag building at that
//    moment is forgotten; if the backlog is real it builds again.
// A camera clock that jumps can't look like lag: a jump forward lowers the
// offset (absorbed), a jump back is re-anchored. Only frames arriving later
// than their camera time says they should, frame after frame, add up.
//
// The two levels on top of it:
//  - lagging(): lag over LAG_MS for LAG_SUSTAIN_MS. Logged and counted.
//  - wants_reconnect(): lag over RECONNECT_LAG_MS for RECONNECT_SUSTAIN_MS
//    and not falling (within DRAIN_MARGIN_MS of its peak since it went over),
//    or over LAG_MS for STUCK_MS (behind, and not caught up). Reconnecting
//    drops the backlog and returns to live, but the dropped frames are never
//    recorded and it costs seconds (RTSP setup, then waiting for a key
//    frame), so a backlog that is draining by itself (after a Wi-Fi stall or
//    a jitterbuffer wait) is left to drain, with STUCK_MS as the backstop.
// An episode ends when lag falls back under CAUGHT_UP_MS. Lag that never
// lasted LAG_SUSTAIN_MS isn't an episode and isn't logged.
class r_lag_watchdog
{
public:
    enum
    {
        LAG_MS = 500,
        LAG_SUSTAIN_MS = 2000,
        RECONNECT_LAG_MS = 2000,
        RECONNECT_SUSTAIN_MS = 1000,
        STUCK_MS = 10000,
        DRAIN_MARGIN_MS = 200,
        CAUGHT_UP_MS = 250,
        JUMP_MS = 250,
        DRIFT_ALLOWANCE_PPM = 1000,
        RATE_WINDOW_MS = 5000,
        RATE_SAMPLE_MS = 200
    };

    // What frame() found, for the caller to log.
    enum r_lag_event
    {
        lag_none,
        lag_started,     // lagging() just became true
        lag_caught_up,   // a lagging episode just ended
        lag_clock_jump   // camera time jumped back; re-anchored
    };

    R_API r_lag_event frame(int64_t camera_ms, int64_t arrival_ms);

    // The last frame's lag behind live, ms (0 before the first frame).
    int64_t lag_ms() const { return _lag_ms; }
    // Peak lag in the current (or last) episode, and how long the last one
    // that ended was behind.
    int64_t peak_lag_ms() const { return _peak_ms; }
    int64_t last_episode_ms() const { return _last_episode_ms; }
    // How long lag has been over LAG_MS, ms (0 when it isn't).
    R_API int64_t behind_for_ms(int64_t arrival_ms) const;
    bool lagging() const { return _lagging; }
    R_API bool wants_reconnect() const;
    // Camera time per arrival time over the last RATE_WINDOW_MS: ~1.0 live,
    // under 1.0 falling behind, over 1.0 catching up. 1.0 with too little data.
    R_API double rate() const;
    // Lagging episodes since construction.
    int episodes() const { return _episodes; }
    int clock_jumps() const { return _clock_jumps; }
    R_API r_lag_snapshot snapshot(int64_t now_ms) const;

private:
    void _reset_episode();

    bool _anchored {false};
    double _baseline {0};          // fractional: it creeps by a fraction of a ms a frame
    int64_t _newest_camera_ms {0};
    int64_t _last_arrival_ms {0};
    int64_t _lag_ms {0};
    int64_t _peak_ms {0};
    int64_t _over_since {-1};      // arrival when lag went over LAG_MS
    int64_t _reconnect_since {-1}; // arrival when lag went over RECONNECT_LAG_MS
    int64_t _reconnect_peak {0};   // peak lag since then
    int64_t _last_episode_ms {0};
    bool _lagging {false};
    int _episodes {0};
    int _clock_jumps {0};
    std::deque<std::pair<int64_t, int64_t>> _samples; // (arrival, camera), one per RATE_SAMPLE_MS
};

// Per camera, kept across reconnects: whether a lag reconnect may happen now,
// and whether the camera is flagged as more than its network can carry. The
// first lag reconnect is immediate; each one within RESET_MS of the last
// doubles the wait before the next (FIRST_HOLD_MS, then 2×, 4×, … up to
// MAX_HOLD_MS). While held off, the camera keeps lagging and keeps being
// logged. FLAG_RECONNECTS lag reconnects within FLAG_WINDOW_MS flag it:
// "network can't keep up with this stream: use the sub-stream or a wired
// link". The flag clears once FLAG_WINDOW_MS passes without one. Thread-safe.
class r_lag_reconnects
{
public:
    enum
    {
        FIRST_HOLD_MS = 30 * 1000,
        MAX_HOLD_MS = 10 * 60 * 1000,
        RESET_MS = 30 * 60 * 1000,
        FLAG_RECONNECTS = 2,
        FLAG_WINDOW_MS = 60 * 60 * 1000
    };

    // True, and recorded as a reconnect at now_ms, if the camera may reconnect
    // now; false while it's held off.
    R_API bool try_reconnect(const std::string& camera_id, int64_t now_ms);
    // ms until the next lag reconnect is allowed (0: now).
    R_API int64_t held_off_ms(const std::string& camera_id, int64_t now_ms) const;
    // Lag reconnects within FLAG_WINDOW_MS before now_ms.
    R_API int recent(const std::string& camera_id, int64_t now_ms) const;
    R_API bool flagged(const std::string& camera_id, int64_t now_ms) const;

private:
    struct state
    {
        std::deque<int64_t> times;
        int64_t next_allowed {0};
        int64_t hold_ms {0};
    };
    mutable std::mutex _mtx;
    std::map<std::string, state> _cameras;
};

R_API extern const char* const LAG_FLAG_MESSAGE;

// Which of a camera's queued key frames the motion engine should look at. A
// person at the door must be seen as soon as their frame arrives, so when the
// engine falls behind (several cameras, a slow decode) it skips to each
// camera's newest key frame rather than working through stale ones: a frame
// is skipped when a newer key frame from the same camera is already queued
// and this one has waited more than STALE_MS. Recording keeps every frame;
// only detection skips. Thread-safe: posted() is called from the cameras'
// threads, skip() from the engine's.
class r_newest_frame_gate
{
public:
    enum { STALE_MS = 500 };

    // A key frame was queued for camera_id; returns its sequence number.
    R_API uint64_t posted(const std::string& camera_id);
    // Whether to skip a frame with this sequence number that has waited waited_ms.
    R_API bool skip(const std::string& camera_id, uint64_t seq, int64_t waited_ms) const;
    R_API void forget(const std::string& camera_id);

private:
    mutable std::mutex _mtx;
    std::map<std::string, uint64_t> _newest;
};

}

#endif
