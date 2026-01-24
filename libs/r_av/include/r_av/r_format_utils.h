
#ifndef r_mux_r_format_utils_h
#define r_mux_r_format_utils_h

#include "r_utils/r_macro.h"
#include <string>
#include <map>

#define DEMUXER_STREAM_TYPE_UNKNOWN -1

namespace r_av
{
R_API std::string ff_rc_to_msg(int rc);
}

#endif
