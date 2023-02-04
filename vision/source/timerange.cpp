
#include "timerange.h"
#include "r_utils/r_exception.h"
#include <chrono>

using namespace vision;
using namespace std::chrono;
using namespace r_utils;

system_clock::time_point timerange::time_in_range(int min, int max, int val)
{
    if(min >= max)
        R_THROW(("Invalid min/max."));
    if(val < min || val > max)
        R_THROW(("Value out of range."));
    if(_start > _end)
        R_THROW(("Invalid start/end."));

    auto delta = max - min;

    auto frac_val = (double)val / (double)delta;

    auto time_range_millis = duration_cast<milliseconds>(_end - _start).count();

    auto output_offset_millis = (int64_t)(time_range_millis * frac_val);

    return _start + milliseconds(output_offset_millis);
}

bool timerange::time_is_in_range(const system_clock::time_point& time) const
{
    return time >= _start && time <= _end;
}

int timerange::time_to_range(const std::chrono::system_clock::time_point& time, int min, int max) const
{
    if(min >= max)
        R_THROW(("Invalid min/max."));
    if(_start > _end)
        R_THROW(("Invalid start/end."));

    auto delta = max - min;

    auto time_range_millis = duration_cast<milliseconds>(_end - _start).count();

    auto time_offset_millis = duration_cast<milliseconds>(time - _start).count();

    auto frac_val = (double)time_offset_millis / (double)time_range_millis;

    return (int)(frac_val * delta);
}