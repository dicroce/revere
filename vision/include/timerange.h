
#ifndef __vision_timerange_h
#define __vision_timerange_h

#include <chrono>
#include <map>

namespace vision
{

class timerange
{
public:
    timerange() {}
    timerange(const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end)
        : _start(start)
        , _end(end)
    {
    }

    std::chrono::system_clock::time_point time_in_range(int min, int max, int val);
    bool time_is_in_range(const std::chrono::system_clock::time_point& time) const;
    int time_to_range(const std::chrono::system_clock::time_point& time, int min, int max) const;

    void set_start(const std::chrono::system_clock::time_point& start) {_start = start;}
    std::chrono::system_clock::time_point get_start() const {return _start;}

    void set_end(const std::chrono::system_clock::time_point& end) {_end = end;}
    std::chrono::system_clock::time_point get_end() const {return _end;}

    std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point> get() const
    {
        return std::make_pair(_start, _end);
    }

    int64_t duration_milliseconds() const
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(_end - _start).count();
    }

private:
    std::chrono::system_clock::time_point _start;
    std::chrono::system_clock::time_point _end;
};

}

#endif
