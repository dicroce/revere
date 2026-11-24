#ifndef r_av_r_failure_run_h
#define r_av_r_failure_run_h

#include <chrono>
#include <cstdint>

namespace r_av
{

// A run of failures one after another (a decoder refusing every packet from a lost frame to the next key frame: tens of
// thousands of lines in two hours). It says which of them to log: the first, then one every `every`, each with the count
// so far. When a success ends the run, recovered() says how many were left out, so the log can say that once.
class r_failure_run
{
public:
    explicit r_failure_run(std::chrono::milliseconds every = std::chrono::seconds(10)) : _every(every) {}

    // One more failure, at `now`. True if this one is logged: the first of a run, or `every` since the last line.
    bool failed(std::chrono::steady_clock::time_point now)
    {
        ++_count;
        if(_count == 1 || now - _last_logged >= _every)
        {
            _last_logged = now;
            _logged_through = _count;
            return true;
        }
        return false;
    }

    // How many failures in this run, including the one just counted.
    uint64_t count() const { return _count; }

    // A success: the run is over. Returns how many failures of it weren't logged (0 for no run, or all logged), and
    // total (if given) the length of the run.
    uint64_t recovered(uint64_t* total = nullptr)
    {
        auto left_out = _count - _logged_through;
        if(total)
            *total = _count;
        _count = 0;
        _logged_through = 0;
        return left_out;
    }

private:
    std::chrono::milliseconds _every;
    std::chrono::steady_clock::time_point _last_logged{};
    uint64_t _count {0};
    uint64_t _logged_through {0};
};

}

#endif
