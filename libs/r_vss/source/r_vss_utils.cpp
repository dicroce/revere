
#include "r_vss/r_vss_utils.h"
#include <iterator>
#include <algorithm>
#include <numeric>
#include <deque>
#include <cstdint>

using namespace std;

constexpr double kGapInflation = 1.25;

vector<pair<int64_t, int64_t>> r_vss::find_contiguous_segments(const vector<int64_t>& times)
{
    using pair64 = std::pair<int64_t,int64_t>;
    std::vector<pair64> segments;

    const std::size_t n = times.size();
    if (n == 0) return segments;                      // empty input
    if (n == 1) { segments.emplace_back(times[0],times[0]); return segments; }

    /* ---------- 1. Compute a robust “normal” frame spacing ------------ */
    std::vector<int64_t> deltas;
    deltas.reserve(n - 1);
    for (std::size_t i = 1; i < n; ++i)
        deltas.push_back(times[i] - times[i-1]);

    // Median guards against a single large gap skewing the metric.
    std::nth_element(begin(deltas), begin(deltas) + deltas.size()/2, end(deltas));
    const int64_t median = deltas[deltas.size()/2];

    const int64_t time_threshold =
        static_cast<int64_t>(median * kGapInflation); // round-toward-zero is fine for gaps in µs/ms

    /* ---------- 2. Single pass to build [start,end] pairs ------------- */
    segments.reserve(n);                              // upper bound
    int64_t seg_start = times.front();

    for (std::size_t i = 1; i < n; ++i)
    {
        const int64_t delta = times[i] - times[i-1];
        if (delta > time_threshold) {
            segments.emplace_back(seg_start, times[i-1]); // close previous segment
            seg_start = times[i];                         // start new one
        }
    }
    segments.emplace_back(seg_start, times.back());       // final segment
    return segments;
}

