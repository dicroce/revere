#ifndef r_utils__r_time_utils_h
#define r_utils__r_time_utils_h

#include "r_utils/r_macro.h"
#include <chrono>
#include <string>
#include <cstdint>

namespace r_utils
{

namespace r_time_utils
{

R_API std::chrono::system_clock::time_point iso_8601_to_tp(const std::string& str);

R_API std::string tp_to_iso_8601(const std::chrono::system_clock::time_point& tp, bool UTC);

R_API std::chrono::milliseconds iso_8601_period_to_duration(const std::string& str);

R_API std::string duration_to_iso_8601_period(std::chrono::milliseconds d);

R_API int64_t tp_to_epoch_millis(const std::chrono::system_clock::time_point& tp);

R_API std::chrono::system_clock::time_point epoch_millis_to_tp(int64_t t);

template<typename INT>
R_API INT convert_clock_freq(INT ticks, INT srcTicksPerSecond, INT dstTicksPerSecond)
{
    return ticks / srcTicksPerSecond * dstTicksPerSecond +
           ticks % srcTicksPerSecond * dstTicksPerSecond / srcTicksPerSecond;
}

R_API bool is_tz_utc();

}

}

#endif