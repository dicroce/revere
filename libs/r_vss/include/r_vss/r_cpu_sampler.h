#ifndef __r_vss_r_cpu_sampler_h
#define __r_vss_r_cpu_sampler_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <deque>

namespace r_vss
{

// How busy the whole machine's CPU is, for the pressure level (r_pressure.h). Not revere's own use: the machine's, as
// every program on it uses it, since what revere's features compete with is all of it.

// The machine's CPU time counters, summed over its cores: busy and total (busy + idle) since boot, in any one unit.
struct r_cpu_ticks
{
    uint64_t busy {0};
    uint64_t total {0};
};

// Reads them (host_processor_info on macOS, /proc/stat on Linux, GetSystemTimes on Windows). False if it can't.
R_API bool read_cpu_ticks(r_cpu_ticks& ticks);

// The number of logical cores.
R_API int cpu_core_count();

// What a series of samples says. add() is given each reading with its time (the sampler thread reads every 2 s), so
// the windows are tested with made-up times and readings; nothing here reads the clock.
class r_cpu_sampler
{
public:
    static constexpr int64_t AVERAGE_WINDOW_MS = 10 * 1000;     // the busy figure is the average of this much
    static constexpr double ELEVATED_BUSY = 0.75;                // busy at or over this ...
    static constexpr double HIGH_BUSY = 0.90;                    // ... or this, with the average ...
    static constexpr int64_t ELEVATED_FOR_MS = 30 * 1000;        // ... held this long is elevated
    static constexpr int64_t HIGH_FOR_MS = 10 * 1000;            // ... and this long is high
    static constexpr size_t DAY_MINUTES = 24 * 60;               // the 95th percentile is over this many per-minute averages

    // A reading: the counters at now_ms. The first one only sets the starting point; each later one gives the busy
    // fraction since the one before. A reading with no time passing (the counters didn't move) or counters that went
    // back (a reset) is ignored.
    R_API void add(int64_t now_ms, const r_cpu_ticks& ticks);

    // The average busy fraction (0 to 1) over the last AVERAGE_WINDOW_MS; -1 before there is a sample.
    R_API double busy() const;

    // How long the average has been continuously at or over ELEVATED_BUSY / HIGH_BUSY, as of now; 0 when it isn't.
    R_API int64_t elevated_for_ms(int64_t now_ms) const;
    R_API int64_t high_for_ms(int64_t now_ms) const;

    // The 95th percentile of the per-minute averages over the last day; -1 before a minute has passed.
    R_API double busy_p95_24h() const;

    R_API bool has_sample() const { return !_window.empty(); }

private:
    struct sample { int64_t at_ms; double busy; };

    bool _have_last {false};
    r_cpu_ticks _last;
    std::deque<sample> _window;
    // Since when the 10 s average has held at each line (-1: not holding).
    int64_t _over_elevated_since {-1};
    int64_t _over_high_since {-1};
    // The minute being summed, and the finished minutes' averages.
    int64_t _minute {-1};
    double _minute_sum {0.0};
    int _minute_n {0};
    std::deque<double> _minutes;
};

}

#endif
