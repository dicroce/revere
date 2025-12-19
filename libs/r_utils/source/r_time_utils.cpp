#define _CRT_SECURE_NO_WARNINGS

// Include date.h before Windows headers to avoid macro conflicts
#include "r_utils/3rdparty/date/date.h"

#include "r_utils/r_time_utils.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include <string.h>
#include <chrono>
#include <sstream>
#include <ctime>
#include <iomanip>

using namespace r_utils;
using namespace std;
using namespace std::chrono;

system_clock::time_point r_utils::r_time_utils::iso_8601_to_tp(const string& str)
{
	// 1976-10-01T12:00:00.000+0:00   Interesting cases because time is essentially in UTC but
	// 1976-10-01T12:00:00.000-0:00   there is no trailing Z.
	//
	// 1976-10-01T12:00:00.000-7:00   Los Angeles
	//    local time is 12pm and that is behind utc by 7 hours
	//
	// 1976-10-01T12:00:00.000+3:00   Moscow
	//    local time is 12pm and that is ahead of utc by 3 hours

    system_clock::time_point tp;
    std::istringstream ss{str};

    // Try different ISO 8601 formats in order of likelihood
    // Format 1: With 'Z' suffix (UTC) - e.g., "2024-01-01T12:00:00.000Z"
    if (str.find('Z') != string::npos) {
        ss >> date::parse("%FT%T%Z", tp);
        if (!ss.fail())
            return tp;
    }

    // Format 2: With timezone offset - e.g., "2024-01-01T12:00:00.000+03:00"
    // Look for + or - in the last part of the string (timezone offset)
    auto last_plus = str.rfind('+');
    auto last_minus = str.rfind('-');
    // Make sure it's not the date separator (should be after 'T')
    auto t_pos = str.find('T');
    if ((last_plus != string::npos && last_plus > t_pos) ||
        (last_minus != string::npos && last_minus > t_pos && last_minus > 8)) {
        ss.clear();
        ss.str(str);
        ss >> date::parse("%FT%T%Ez", tp);  // %Ez handles ±HH:MM format
        if (!ss.fail())
            return tp;
    }

    // Format 3: No timezone (treat as local time) - e.g., "2024-01-01T12:00:00.000"
    // Parse the time components and convert from local time to UTC
    ss.clear();
    ss.str(str);
    ss >> date::parse("%FT%T", tp);
    if (!ss.fail())
    {
        // tp now contains the parsed time as if it were UTC, but it represents local time
        // We need to convert from local time to UTC
        auto tp_ms = floor<milliseconds>(tp);
        time_t parsed_time = system_clock::to_time_t(tp_ms);

        // Use mktime to interpret the parsed time as local time and get UTC equivalent
        struct tm local_tm;
#ifdef IS_WINDOWS
        gmtime_s(&local_tm, &parsed_time);  // Get the tm struct in UTC (which is what we parsed as)
#else
        gmtime_r(&parsed_time, &local_tm);
#endif
        // mktime interprets tm as local time and returns UTC time_t
        time_t utc_time = mktime(&local_tm);

        // Calculate the offset between local and UTC
        time_t offset = parsed_time - utc_time;

        // Apply offset to get true UTC time
        return tp_ms - seconds(offset);
    }

    // Format 4: Malformed format without colons - e.g., "2025-11-14T160437.070"
    // This handles the macOS bug where colons are missing from the time portion
    // Also treat as local time and convert to UTC
    ss.clear();
    ss.str(str);
    ss >> date::parse("%FT%H%M%S", tp);
    if (!ss.fail())
    {
        auto tp_ms = floor<milliseconds>(tp);
        time_t parsed_time = system_clock::to_time_t(tp_ms);

        struct tm local_tm;
#ifdef IS_WINDOWS
        gmtime_s(&local_tm, &parsed_time);
#else
        gmtime_r(&parsed_time, &local_tm);
#endif
        time_t utc_time = mktime(&local_tm);
        time_t offset = parsed_time - utc_time;

        return tp_ms - seconds(offset);
    }

    // If all parsing attempts failed, throw an exception
    R_THROW(("Failed to parse ISO 8601 timestamp: %s", str.c_str()));
}

string r_utils::r_time_utils::tp_to_iso_8601(const system_clock::time_point& tp, bool UTC)
{
#ifdef IS_MACOS
    // macOS workaround: The date library format string with colons doesn't work correctly on macOS
    // Use manual formatting instead. Always use UTC with 'Z' suffix to avoid timezone ambiguity.
    auto tp_millis = floor<milliseconds>(tp);
    auto tp_seconds = floor<seconds>(tp_millis);
    auto millis = duration_cast<milliseconds>(tp_millis - tp_seconds).count();

    time_t tt = system_clock::to_time_t(tp_seconds);
    struct tm timeinfo;
    gmtime_r(&tt, &timeinfo);

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec,
             millis);
    return string(buffer);
#else
    if (UTC) {
        // Format with UTC 'Z' suffix
        // %F = %Y-%m-%d, %H:%M:%S includes hours:minutes:seconds, %S includes fractional seconds
        //  We use separate %H:%M:%S instead of %T to ensure milliseconds are included
        return date::format("%FT%H:%M:%SZ", floor<milliseconds>(tp));
    } else {
        // Convert UTC time_point to local time and format without timezone suffix
        auto tp_ms = floor<milliseconds>(tp);
        time_t utc_time = system_clock::to_time_t(tp_ms);
        auto sub_second_ms = duration_cast<milliseconds>(tp_ms - floor<seconds>(tp_ms)).count();

        struct tm local_tm;
#ifdef IS_WINDOWS
        localtime_s(&local_tm, &utc_time);
#else
        localtime_r(&utc_time, &local_tm);
#endif
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03lld",
                 local_tm.tm_year + 1900,
                 local_tm.tm_mon + 1,
                 local_tm.tm_mday,
                 local_tm.tm_hour,
                 local_tm.tm_min,
                 local_tm.tm_sec,
                 (long long)sub_second_ms);
        return string(buffer);
    }
#endif
}

milliseconds r_utils::r_time_utils::iso_8601_period_to_duration(const string& str)
{
	auto dur = milliseconds::zero();

	char designators[] = {'Y', 'M', 'W', 'D', 'T', 'H', 'M', 'S'};

	size_t idx = 0;

	auto prevDesig = 'P';

	bool parsedDate = false;

	for(size_t i = 0; i < 8; ++i)
	{
		auto didx = str.find(designators[i], idx);
		idx = didx + 1;

		if(didx != string::npos)
		{
			//auto fieldStart = str.rfind_first_not_of("0123456789");
			auto fieldStart = str.rfind(prevDesig, didx) + 1;

			auto field = str.substr(fieldStart, (didx - fieldStart));

            auto val = r_string_utils::s_to_size_t(field);

			if(!parsedDate)
			{
				switch(designators[i])
				{
					case 'Y'://YEARS
						dur += hours(val * 8760);
					break;
					case 'M'://MONTHS
						dur += hours(val * 720);
					break;
					case 'W'://WEEKS
						dur += hours(val * 168);
					break;
					case 'D'://DAYS
						dur += hours(val * 24);
					break;
					case 'T':
						parsedDate = true;
					break;
                	default:
                    	R_THROW(("Unknown iso 8601 duration designator 1:"));
            	};
			}
			else
			{
				switch(designators[i])
				{
					case 'H'://HOURS
						dur += hours(val);
					break;
					case 'M'://MINUTES
						dur += minutes(val);
					break;
					case 'S'://SECONDS
					{
						if(field.find(".") != std::string::npos)
						{
							auto dval = r_string_utils::s_to_double(field);
							size_t wholeSeconds = (size_t)dval;
							double fracSeconds = dval - wholeSeconds;
							dur += seconds(wholeSeconds);
							size_t millis = (size_t)(fracSeconds * (double)1000);
							dur += milliseconds(millis);
						}
						else dur += seconds(val);
					}
					break;
					default:
						R_THROW(("Unknown iso 8601 duration designator 2:"));
				};
			}

			prevDesig = designators[i];
		}
	}


	return dur;
}

string r_utils::r_time_utils::duration_to_iso_8601_period(milliseconds d)
{
	string output = "P";

    auto y = duration_cast<hours>(d).count() / 8760;
    d -= hours(y * 8760);
	if(y > 0)
		output += r_string_utils::format("%dY", y);

    auto mo = duration_cast<hours>(d).count() / 720;
    d -= hours(mo * 720);
	if(mo > 0)
		output += r_string_utils::format("%dM", mo);

    auto w = duration_cast<hours>(d).count() / 168;
    d -= hours(w * 168);
	if(w > 0)
		output += r_string_utils::format("%dW", w);

    auto da = duration_cast<hours>(d).count() / 24;
    d -= hours(da * 24);
	if(da > 0)
		output += r_string_utils::format("%dD", da);

    auto h = duration_cast<hours>(d).count();
    d -= hours(h);

    auto m = duration_cast<minutes>(d).count();
    d -= minutes(m);

    auto s = duration_cast<seconds>(d).count();
    d -= seconds(s);

    auto ms = duration_cast<milliseconds>(d).count();

	if(h > 0 || m > 0 || s > 0 || ms > 0)
		output += "T";

	if(h > 0)
		output += r_string_utils::format("%dH", h);

	if(m > 0)
		output += r_string_utils::format("%dM", m);

	if(s > 0)
		output += r_string_utils::format("%lld", s);

    if(ms > 0)
    {
		if(s == 0)
			output += "0";

    	auto frac = r_string_utils::double_to_s((double)ms / 1000.f).substr(2);
        frac.erase(frac.find_last_not_of('0') + 1, std::string::npos);
		output += r_string_utils::format(".%sS",frac.c_str());
    }
    else
	{
		if(s > 0)
			output += "S";
	}

    return output;
}

int64_t r_utils::r_time_utils::tp_to_epoch_millis(const chrono::system_clock::time_point& tp)
{
	return duration_cast<milliseconds>(tp.time_since_epoch()).count();
}

chrono::system_clock::time_point r_utils::r_time_utils::epoch_millis_to_tp(int64_t t)
{
	return system_clock::time_point() + milliseconds(t);
}

bool r_utils::r_time_utils::is_tz_utc()
{
    time_t ofs = 0;
    struct tm timeInfo;
#ifdef IS_WINDOWS
    if(localtime_s(&timeInfo, &ofs) != 0)
        R_THROW(("Unable to query local time."));
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
	timeInfo = *localtime(&ofs);
#endif
    return (timeInfo.tm_hour == 0);
}
