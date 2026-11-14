#ifndef __r_av_r_bitrate_control_h
#define __r_av_r_bitrate_control_h

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>

namespace r_av
{

/**
 * The transcoder's bitrate target, moved from what the encoder produced in each window (a few seconds).
 *
 * Over 1.15 times the target it cuts the target by 30%; under 0.85 times it raises it by 25%; both within
 * [min, max], and only when the move is more than 10% of the target. This is the rule r_transcoder always had.
 *
 * The first window after a start is skipped: it holds the encoder's first key frames, which are far larger than
 * the rest, and a stream's start may also send its first seconds unpaced. Judged on that burst the target was cut
 * on every start (a gateway's log: 2.0 to 1.4 Mbps at the first check, each time), and then the picture started
 * soft. The next windows are judged as before.
 */
class r_bitrate_control
{
public:
    r_bitrate_control(uint32_t initial, uint32_t min_bitrate, uint32_t max_bitrate) :
        _current(initial), _min(min_bitrate), _max(max_bitrate), _skipped_first(false) {}

    uint32_t current() const { return _current; }

    /** A new start: the first window is skipped again. The target stays where it was. */
    void restart() { _skipped_first = false; }

    /** Bits per second for `bytes` over `window_ms` (0 for an empty window). */
    static uint32_t bits_per_second(size_t bytes, uint32_t window_ms)
    {
        return window_ms == 0 ? 0 : static_cast<uint32_t>((static_cast<uint64_t>(bytes) * 8 * 1000) / window_ms);
    }

    /**
     * One window ended, with `bytes` encoded over `window_ms` milliseconds: what really elapsed, not the 5 s it was
     * meant to be (it is checked on a frame, so it runs over, and a rate over a fixed 5 s read up to 20% high, past the
     * 1.15 threshold). The new target when it changes, otherwise nothing.
     */
    std::optional<uint32_t> review(size_t bytes, uint32_t window_ms)
    {
        if(!_skipped_first)
        {
            _skipped_first = true;
            return std::nullopt;
        }
        if(window_ms == 0)
            return std::nullopt;

        const uint32_t actual = bits_per_second(bytes, window_ms);
        const float ratio = (float)actual / (float)_current;
        uint32_t next = _current;
        if(ratio < 0.85f && _current < _max)
            next = (std::min)(_max, (uint32_t)(_current * 1.25f));
        else if(ratio > 1.15f)
            next = (std::max)(_min, (uint32_t)(_current * 0.70f));

        if(std::abs((int)next - (int)_current) > (int)(_current * 0.10f))
        {
            _current = next;
            return next;
        }
        return std::nullopt;
    }

private:
    uint32_t _current;
    uint32_t _min;
    uint32_t _max;
    bool _skipped_first;
};

}

#endif
