#ifndef __r_vss_r_ws_client_errors_h
#define __r_vss_r_ws_client_errors_h

#include "r_http/r_http_exception.h"
#include "r_utils/r_exception.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_utils/r_time_utils.h"
#include <chrono>
#include <stdexcept>
#include <string>

// What a handler's catch-all lets through to the web server, which answers it as its 4xx (or other status) with
// {"error": why} and one log line (r_web_server.h), instead of logging a stack trace and answering 500: an
// r_http_exception thrown on purpose (the request's own input, through request_time, request_number and request_json
// below), and a resource that isn't there (an unknown camera: r_not_found_exception). Nothing else: the same parse
// failing on the server's own data (a stored codec parameter, a truncated blob tree) is a fault, and stays a logged 500.
#define R_WS_PASS_CLIENT_ERRORS \
    catch(const r_http::r_http_exception&) { throw; } \
    catch(const r_utils::r_not_found_exception&) { throw; }

namespace r_vss
{

// The request's input, quoted in an error: cut short, so a long value doesn't fill the answer and the log.
inline std::string request_value_shown(const std::string& value)
{
    return value.size() > 64 ? value.substr(0, 64) + "..." : value;
}

// A time from the request (a query argument): 400 when it isn't ISO 8601.
inline std::chrono::system_clock::time_point request_time(const std::string& value, const char* name)
{
    try
    {
        return r_utils::r_time_utils::iso_8601_to_tp(value);
    }
    catch(const std::exception&)
    {
        R_STHROW(r_http::r_http_400_exception, ("%s isn't an ISO 8601 time: %s", name, request_value_shown(value).c_str()));
    }
}

// A number from the request with `parse` (an r_string_utils::s_to_*): 400 when it isn't one.
template<class F>
inline auto request_number(const std::string& value, const char* name, F parse) -> decltype(parse(value))
{
    try
    {
        return parse(value);
    }
    catch(const std::exception&)
    {
        R_STHROW(r_http::r_http_400_exception, ("%s isn't a number: %s", name, request_value_shown(value).c_str()));
    }
}

// The request's body as a JSON object: 400 when it isn't one.
inline nlohmann::json request_json(const std::string& body)
{
    auto j = nlohmann::json::parse(body, nullptr, false);
    if(j.is_discarded() || !j.is_object())
        R_STHROW(r_http::r_http_400_exception, ("The request body isn't a JSON object."));
    return j;
}

}

#endif
