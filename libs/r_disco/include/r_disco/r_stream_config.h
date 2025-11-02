
#ifndef r_disco_r_stream_config_h
#define r_disco_r_stream_config_h

#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <array>

namespace r_disco
{

struct r_stream_config
{
    std::string id;
    r_utils::r_nullable<std::string> camera_name;
    r_utils::r_nullable<std::string> ipv4;
    r_utils::r_nullable<int> port;  // ONVIF service port (discovered from XAddrs)
    r_utils::r_nullable<std::string> protocol;  // ONVIF service protocol (http/https)
    r_utils::r_nullable<std::string> xaddrs;
    r_utils::r_nullable<std::string> address;
    r_utils::r_nullable<std::string> rtsp_url;

    // Streams can be video or audio only (or both) and they may not have any parameters
    r_utils::r_nullable<std::string> video_codec;
    r_utils::r_nullable<std::string> video_codec_parameters;
    r_utils::r_nullable<int> video_timebase;

    r_utils::r_nullable<std::string> audio_codec;
    r_utils::r_nullable<std::string> audio_codec_parameters;
    r_utils::r_nullable<int> audio_timebase;
};

R_API std::string hash_stream_config(const r_stream_config& sc);

}

#endif
