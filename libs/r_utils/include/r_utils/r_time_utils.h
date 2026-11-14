#ifndef r_utils__r_time_utils_h
#define r_utils__r_time_utils_h

#include "r_utils/r_macro.h"
#include <chrono>
#include <string>
#include <cstdint>
#include <ctime>

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

R_API bool is_valid_iso_8601(const std::string& str);

// Thread-safe time_t to broken-down time (localtime_r / gmtime_r). Throw if t
// can't be converted.
R_API std::tm to_local_tm(std::time_t t);
R_API std::tm to_utc_tm(std::time_t t);

// The time_t for a broken-down UTC time (timegm). Normalizes out-of-range
// fields, as mktime does.
R_API std::time_t utc_tm_to_time_t(std::tm t);

// t in local time as ctime() writes it ("Wed Jun 30 21:49:08 1993\n").
R_API std::string to_ctime_string(std::time_t t);

}

}

#endif