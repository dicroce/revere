#ifndef r_av_r_failure_run_h
#define r_av_r_failure_run_h

#include <chrono>
#include <cstdint>

namespace r_av
{

// A burst of failures (a decoder refusing packets from a lost frame to the next key frame: tens of thousands of lines
// in two hours). It says which of them to log: the first, then one every `every`, each with the count so far.
//
// A burst is a stretch of failures with no gap of `every` between them, whether or not an occasional success falls in
// it: a decoder that accepts one packet in ten still fails, and logging each failure because a success "ended the run"
// is the flood again. A success ends the burst only when `every` has passed since the last failure, and recovered()
// then says how many failures of it were left out, so the log can say that once.
class r_failure_run
{
public:
    explicit r_failure_run(std::chrono::milliseconds every = std::chrono::seconds(10)) : _every(every) {}

    void set_every(std::chrono::milliseconds every) { _every = every; }

    // One more failure, at `now`. True if this one is logged: the first of a burst, or `every` since the last line.
    bool failed(std::chrono::steady_clock::time_point now)
    {
        // A burst nobody ended (no success after the quiet) is over too: this failure starts a new one.
        if(_count > 0 && now - _last_failure >= _every)
            _reset();
        _last_failure = now;
        ++_count;
        if(_count == 1 || now - _last_logged >= _every)
        {
            _last_logged = now;
            _logged_through = _count;
            return true;
        }
        return false;
    }

    // How many failures in this burst, including the one just counted.
    uint64_t count() const { return _count; }

    // A success, at `now`. While failures are still coming (the last was less than `every` ago) it changes nothing and
    // returns 0. Otherwise the burst is over: returns how many of its failures weren't logged (0 for no burst, or all
    // logged), and total (if given) the length of the burst.
    uint64_t recovered(std::chrono::steady_clock::time_point now, uint64_t* total = nullptr)
    {
        if(_count == 0)
        {
            if(total)
                *total = 0;
            return 0;
        }
        if(now - _last_failure < _every)
            return 0;
        auto left_out = _count - _logged_through;
        if(total)
            *total = _count;
        _reset();
        return left_out;
    }

private:
    void _reset()
    {
        _count = 0;
        _logged_through = 0;
    }

    std::chrono::milliseconds _every;
    std::chrono::steady_clock::time_point _last_failure{};
    std::chrono::steady_clock::time_point _last_logged{};
    uint64_t _count {0};
    uint64_t _logged_through {0};
};

}

#endif
