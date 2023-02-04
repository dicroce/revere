#define _CRT_SECURE_NO_WARNINGS
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

    auto zpos = str.find('Z');
    auto without_z = (zpos == std::string::npos) ? str : str.substr(0, zpos);

    auto ppos = without_z.find(".");
    double frac = (ppos != std::string::npos) ? stod(without_z.substr(ppos)) : 0.0;

    std::istringstream ss(without_z);
    std::tm bdt;
    memset(&bdt, 0, sizeof(bdt));
    ss >> std::get_time(&bdt, "%Y-%m-%dT%H:%M:%S");
	
	bdt.tm_isdst = -1;

    time_t theTime;
    if (zpos != std::string::npos)
    {
#ifdef IS_LINUX
        theTime = mktime(&bdt);
#endif
#ifdef IS_WINDOWS
        theTime = _mkgmtime(&bdt);
#endif
    }
    else
    {
        theTime = mktime(&bdt);
    }

    return std::chrono::system_clock::from_time_t(theTime) + std::chrono::milliseconds((int)(frac * 1000));
}

string r_utils::r_time_utils::tp_to_iso_8601(const system_clock::time_point& tp, bool UTC)
{
    // convert to time_t while preserving fractional part.
    auto tp_seconds = std::chrono::system_clock::to_time_t(tp);
    auto frac_millis = std::chrono::duration_cast<std::chrono::milliseconds>(tp - std::chrono::system_clock::from_time_t(tp_seconds)).count();

    // Use std::localtime() or std::gmtime() to convert to std:tm (bdt)
    std::tm bdt = (UTC) ? *std::gmtime(&tp_seconds) : *std::localtime(&tp_seconds);

    std::string output;
    std::stringstream output_stream(output);

    // Use std::put_time to convert broken down time to a string.
    output_stream << std::put_time(&bdt, "%FT%T");

    // Append fractional part to string.
    double fracV = ((double)frac_millis) / 1000.0;
    auto frac = r_string_utils::format("%0.3f", fracV);
    output_stream << frac.substr(frac.find("."));

    if (UTC)
        output_stream << "Z";

    return output_stream.str();
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
							auto val = r_string_utils::s_to_double(field);
							size_t wholeSeconds = (size_t)val;
							double fracSeconds = val - wholeSeconds;
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

uint64_t r_utils::r_time_utils::tp_to_epoch_millis(const chrono::system_clock::time_point& tp)
{
	return duration_cast<milliseconds>(tp.time_since_epoch()).count();
}

chrono::system_clock::time_point r_utils::r_time_utils::epoch_millis_to_tp(uint64_t t)
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
#ifdef IS_LINUX
	timeInfo = *localtime(&ofs);
#endif
    return (timeInfo.tm_hour == 0);
}
