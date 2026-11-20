#ifndef __r_vss_r_ws_client_errors_h
#define __r_vss_r_ws_client_errors_h

#include "r_http/r_http_exception.h"
#include "r_utils/r_exception.h"
#include "r_utils/3rdparty/json/json.h"
#include <stdexcept>

// A client's mistake inside a handler's try, passed on so the web server answers it as a 4xx with {"error": why} and
// one log line (r_web_server.h), instead of the catch-all after this logging a stack trace and answering 500:
// a 4xx thrown on purpose, a resource that isn't there (an unknown camera: r_not_found_exception), a bad argument
// (a time that isn't ISO 8601: r_invalid_argument_exception), a request body that isn't the JSON asked for, and a
// number that isn't one (r_string_utils::s_to_*).
#define R_WS_PASS_CLIENT_ERRORS \
    catch(const r_http::r_http_exception&) { throw; } \
    catch(const r_utils::r_not_found_exception&) { throw; } \
    catch(const r_utils::r_invalid_argument_exception&) { throw; } \
    catch(const nlohmann::json::exception& bad_json) \
    { \
        R_STHROW(r_http::r_http_400_exception, ("The request isn't the JSON expected: %s", bad_json.what())); \
    } \
    catch(const std::invalid_argument&) \
    { \
        R_STHROW(r_http::r_http_400_exception, ("A number in the request isn't valid.")); \
    }

#endif
