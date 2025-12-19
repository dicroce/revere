#include "r_utils/r_weekly_schedule.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/3rdparty/json/json.h"
#include <ctime>
#include <algorithm>

using namespace r_utils;
using namespace std;
using json = nlohmann::json;

r_weekly_schedule::r_weekly_schedule(const string& json_str)
{
    from_json(json_str);
}

r_weekly_schedule::r_weekly_schedule(
    const string& start_time,
    const chrono::seconds& duration,
    const vector<int>& days_of_week
) :
    _start_time(start_time),
    _duration(duration),
    _days(days_of_week)
{
    // Validate start_time format
    auto parts = r_string_utils::split(_start_time, ":");
    if(parts.size() < 2 || parts.size() > 3)
        R_THROW(("Invalid start_time format. Expected HH:MM or HH:MM:SS, got: %s", _start_time.c_str()));

    // Validate days are in ISO range 1-7
    for(auto d : _days)
    {
        if(d < 1 || d > 7)
            R_THROW(("Invalid day of week: %d. Expected 1 (Monday) through 7 (Sunday).", d));
    }
}

r_schedule_state r_weekly_schedule::query(const chrono::system_clock::time_point& tp) const
{
    if(_days.empty() || _duration.count() == 0)
        return r_schedule_state::OUTSIDE;

    // Convert time_point to local time
    auto time_t_val = chrono::system_clock::to_time_t(tp);
    tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &time_t_val);
#else
    localtime_r(&time_t_val, &local_tm);
#endif

    // Get current day of week in ISO format (1=Monday, 7=Sunday)
    // tm_wday is 0=Sunday, 1=Monday, ..., 6=Saturday
    int iso_day = (local_tm.tm_wday == 0) ? 7 : local_tm.tm_wday;

    // Parse start time
    auto parts = r_string_utils::split(_start_time, ":");
    int start_hour = r_string_utils::s_to_int(parts[0]);
    int start_min = r_string_utils::s_to_int(parts[1]);
    int start_sec = (parts.size() > 2) ? r_string_utils::s_to_int(parts[2]) : 0;

    // Calculate seconds since midnight for start time and current time
    int64_t start_seconds = start_hour * 3600 + start_min * 60 + start_sec;
    int64_t current_seconds = local_tm.tm_hour * 3600 + local_tm.tm_min * 60 + local_tm.tm_sec;
    int64_t duration_seconds = _duration.count();

    // Check if we're in a block that started today
    if(find(_days.begin(), _days.end(), iso_day) != _days.end())
    {
        // Block started today - check if we're within duration
        if(current_seconds >= start_seconds && current_seconds < start_seconds + duration_seconds)
            return r_schedule_state::INSIDE;
    }

    // Check if we're in a block that started yesterday and spans into today
    int yesterday_iso = (iso_day == 1) ? 7 : iso_day - 1;
    if(find(_days.begin(), _days.end(), yesterday_iso) != _days.end())
    {
        // Check if yesterday's block extends into today
        int64_t seconds_since_midnight = current_seconds;
        int64_t yesterday_end = start_seconds + duration_seconds - 86400;  // How far into today the block extends

        if(yesterday_end > 0 && seconds_since_midnight < yesterday_end)
            return r_schedule_state::INSIDE;
    }

    return r_schedule_state::OUTSIDE;
}

string r_weekly_schedule::to_json() const
{
    json j;
    j["start_time"] = _start_time;
    j["duration_seconds"] = _duration.count();
    j["days"] = _days;
    return j.dump();
}

void r_weekly_schedule::from_json(const string& json_str)
{
    auto j = json::parse(json_str);

    if(j.contains("start_time"))
        _start_time = j["start_time"].get<string>();

    if(j.contains("duration_seconds"))
        _duration = chrono::seconds(j["duration_seconds"].get<int64_t>());

    if(j.contains("days"))
        _days = j["days"].get<vector<int>>();
}
