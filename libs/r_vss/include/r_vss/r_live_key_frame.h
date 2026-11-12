#ifndef __r_vss_r_live_key_frame_h
#define __r_vss_r_live_key_frame_h

#include <cstdint>
#include <string>
#include <vector>

namespace r_vss
{

// A video key frame as a camera's stream delivered it, with what a decoder needs: when it was stored (epoch ms), the
// codec's name and parameters, and the frame's bytes.
struct r_live_key_frame
{
    int64_t ts {0};
    std::string codec_name;
    std::string codec_parameters;
    std::vector<uint8_t> data;
};

}

#endif
