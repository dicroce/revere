
#include "r_mux/r_format_utils.h"
#include <stdexcept>

extern "C"
{
#include "libavutil/error.h"
#include "libavcodec/avcodec.h"
}

using namespace std;

string r_mux::ff_rc_to_msg(int rc)
{
    char msg_buffer[1024];
    if(av_strerror(rc, msg_buffer, 1024) < 0)
    {
        throw runtime_error("Unknown ff return code.");
    }
    return string(msg_buffer);
}
