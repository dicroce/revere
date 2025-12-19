#ifndef r_utils_r_weekly_schedule_h
#define r_utils_r_weekly_schedule_h

#include "r_utils/r_macro.h"
#include <chrono>
#include <string>
#include <vector>

namespace r_utils
{

enum class r_schedule_state { INSIDE, OUTSIDE };

class r_weekly_schedule final
{
public:
    R_API r_weekly_schedule() = default;
    R_API r_weekly_schedule(const std::string& json);

    R_API r_weekly_schedule(
        const std::string& start_time,              // "HH:MM:SS" local time
        const std::chrono::seconds& duration,
        const std::vector<int>& days_of_week        // 1=Monday ... 7=Sunday (ISO 8601)
    );

    R_API r_weekly_schedule(const r_weekly_schedule& other) = default;
    R_API r_weekly_schedule(r_weekly_schedule&& other) noexcept = default;
    R_API r_weekly_schedule& operator=(const r_weekly_schedule& other) = default;
    R_API r_weekly_schedule& operator=(r_weekly_schedule&& other) noexcept = default;

    R_API ~r_weekly_schedule() = default;

    // Query - tp is UTC, converted to local time for matching
    R_API r_schedule_state query(const std::chrono::system_clock::time_point& tp) const;

    // Serialization
    R_API std::string to_json() const;
    R_API void from_json(const std::string& json);

private:
    std::string _start_time {"00:00:00"};
    std::chrono::seconds _duration {0};
    std::vector<int> _days;
};

}

#endif
