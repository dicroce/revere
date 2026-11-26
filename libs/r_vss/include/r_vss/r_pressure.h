#ifndef __r_vss_r_pressure_h
#define __r_vss_r_pressure_h

#include "r_vss/r_cpu_sampler.h"
#include "r_utils/r_macro.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace r_vss
{

// Revere's own pressure: whether its features are slipping, so a plugin that can wait (HomeKit's conversions) gives
// way before revere's recording and detection do. Revere judges it because only revere knows when its own features
// slip; plugins never guess from CPU alone. The level is the worst signal's, with a reason for every signal that
// holds. A signal with no source (or no data yet) is "unknown": it never raises the level and has no reason, but it
// is listed so what is missing shows.
//
// The thresholds are starting values (the HomeKit modes plan's spike retunes them).

enum class pressure_level { ok, elevated, high };
enum class signal_state { unknown, ok, elevated, high };

R_API const char* pressure_level_name(pressure_level level);
R_API const char* signal_state_name(signal_state state);

// Motion detection falling behind: its queue and its drops.
constexpr size_t PRESSURE_MOTION_QUEUE_ELEVATED = 250;          // of 1000 (MOTION_ENGINE_MAX_QUEUE_SIZE)
constexpr int64_t PRESSURE_MOTION_RECENT_MS = 30 * 1000;        // a skip or a drop this recent counts
// Detection slower than usual: the 1 minute 90th percentile against its own 1 hour baseline.
constexpr double PRESSURE_DETECTION_ELEVATED_RATIO = 1.5;
constexpr double PRESSURE_DETECTION_HIGH_RATIO = 2.5;
constexpr int64_t PRESSURE_DETECTION_HIGH_MS = 2000;
// Recording writes behind: the longest wait of a frame to be written.
constexpr int64_t PRESSURE_WRITE_ELEVATED_MS = 1000;
constexpr int64_t PRESSURE_WRITE_HIGH_MS = 3000;                // or growing for PRESSURE_WRITE_GROWING_MS
constexpr int64_t PRESSURE_WRITE_GROWING_MS = 10 * 1000;
// CPU busy: r_cpu_sampler's ELEVATED_BUSY / HIGH_BUSY held for ELEVATED_FOR_MS / HIGH_FOR_MS.
// Hardware encoder sessions: within one of the limit is elevated, at it is high.
constexpr int PRESSURE_HW_ELEVATED_FREE = 1;

struct r_pressure_inputs
{
    // A bool says whether its signal has a source and data; without it the signal is unknown.
    bool motion_known {false};
    int64_t motion_skip_age_ms {-1};      // how long since the newest-frame gate skipped a stale key frame (-1: never)
    size_t motion_queue {0};              // frames queued for motion detection now
    int64_t motion_drop_age_ms {-1};      // how long since a frame was dropped from the motion queue (-1: never)

    bool detection_known {false};
    double detection_p90_ms {0.0};        // the last minute's 90th percentile of key frame queued to analysed
    double detection_baseline_ms {0.0};   // the same over the last hour; 0 when there isn't one yet

    bool writes_known {false};
    int64_t write_delay_ms {0};           // the longest wait of a frame to be written, lately
    bool write_delay_growing {false};     // it has been growing for PRESSURE_WRITE_GROWING_MS

    bool lag_known {false};
    bool lag_camera_flagged {false};      // a camera is flagged as falling behind (r_lag_reconnects)

    bool cpu_known {false};
    int64_t cpu_elevated_for_ms {0};      // the 10 s average at 75% or more for this long
    int64_t cpu_high_for_ms {0};          // ... at 90% or more

    bool hw_known {false};
    int hw_used {0};
    int hw_limit {0};
};

struct r_pressure_signal
{
    std::string id;           // motion_drop, detection_slow, write_delay, camera_lag, cpu_busy, hw_encoder
    signal_state state {signal_state::unknown};
    std::string text;         // the reason, when it holds (elevated or high)
};

struct r_pressure_result
{
    pressure_level level {pressure_level::ok};
    std::vector<r_pressure_signal> signals;    // all six, in the order above
};

// The level and each signal's state from the inputs: no clock, no state.
R_API r_pressure_result evaluate_pressure(const r_pressure_inputs& in);

// The sources a monitor pulls from each tick. Unset means unknown (no source).
struct r_pressure_sources
{
    std::function<bool(r_cpu_ticks&)> cpu_ticks;               // the machine's CPU counters
    std::function<int64_t()> take_write_delay_max;             // longest write wait since the last call, -1 for none
    std::function<size_t()> motion_queue_size;
    std::function<bool()> camera_flagged_lagging;              // any camera flagged as falling behind
    std::function<bool(int& used, int& limit)> hw_sessions;    // false: unknown
};

// Gathers the signals and answers with the level. Revere's parts tell it what they see (note_*), and tick() (every 2 s
// on its own thread, or by hand in tests) pulls the rest and keeps the time windows; the answer is read from any thread.
class r_pressure_monitor
{
public:
    static constexpr int64_t TICK_MS = 2000;

    R_API explicit r_pressure_monitor(r_pressure_sources sources);
    R_API ~r_pressure_monitor();

    R_API void start();
    R_API void stop();

    // A stale key frame was skipped for a newer one (r_newest_frame_gate) / frames were dropped from the motion queue /
    // a key frame took this long from queued to analysed. now_ms is on one monotonic clock for all calls.
    R_API void note_motion_skip(int64_t now_ms);
    R_API void note_motion_dropped(size_t count, int64_t now_ms);
    R_API void note_detection_latency(int64_t ms, int64_t now_ms);

    // One step: reads the counters and sources at now_ms.
    R_API void tick(int64_t now_ms);

    // The inputs and the answer as of now_ms.
    R_API r_pressure_inputs inputs(int64_t now_ms) const;
    R_API r_pressure_result evaluate(int64_t now_ms) const;

    // The JSON handed to plugins (get_pressure) and served as /system_load: {"level", "reasons": [{signal, text}],
    // "signals": [{id, state}], "cpu", "cpu_p95_24h", "cores", "hw_sessions": {used, limit} | null}.
    R_API std::string json(int64_t now_ms) const;

private:
    void _entry_point();

    r_pressure_sources _sources;
    mutable std::mutex _lock;
    r_cpu_sampler _cpu;
    bool _cpu_source_ok {false};
    int64_t _last_skip_ms {-1};
    int64_t _last_drop_ms {-1};
    size_t _motion_queue {0};
    bool _motion_seen {false};
    std::deque<std::pair<int64_t, int64_t>> _detect_recent;    // (at, ms) over the last minute
    std::deque<double> _detect_minutes;                        // the 90th percentile of each of the last hour's minutes
    int64_t _detect_minute {-1};
    std::deque<std::pair<int64_t, int64_t>> _write_ticks;      // (at, delay) over the last PRESSURE_WRITE_GROWING_MS
    int64_t _write_delay_ms {0};
    bool _writes_seen {false};
    bool _lag_known {false};
    bool _lag_flagged {false};
    bool _hw_known {false};
    int _hw_used {0};
    int _hw_limit {0};
    int64_t _now_ms_last {0};

    std::atomic<bool> _running {false};
    std::thread _thread;
};

}

#endif
