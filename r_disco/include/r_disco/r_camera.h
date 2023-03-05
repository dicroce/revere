#ifndef r_disco_r_camera_h
#define r_disco_r_camera_h

#include "r_utils/r_nullable.h"
#include <string>

namespace r_disco
{

struct r_camera
{
    // r_stream_config sourced fields
    std::string id;
    r_utils::r_nullable<std::string> camera_name;
    r_utils::r_nullable<std::string> friendly_name;
    r_utils::r_nullable<std::string> ipv4;
    r_utils::r_nullable<std::string> xaddrs;
    r_utils::r_nullable<std::string> address;
    r_utils::r_nullable<std::string> rtsp_url;
    r_utils::r_nullable<std::string> video_codec;
    r_utils::r_nullable<std::string> video_codec_parameters;
    r_utils::r_nullable<int> video_timebase;
    r_utils::r_nullable<std::string> audio_codec;
    r_utils::r_nullable<std::string> audio_codec_parameters;
    r_utils::r_nullable<int> audio_timebase;

    // r_camera specific fields
    r_utils::r_nullable<std::string> rtsp_username;
    r_utils::r_nullable<std::string> rtsp_password;
    std::string state;
    r_utils::r_nullable<std::string> record_file_path;
    r_utils::r_nullable<int64_t> n_record_file_blocks;
    r_utils::r_nullable<int64_t> record_file_block_size;
    r_utils::r_nullable<bool> do_motion_detection;
    r_utils::r_nullable<std::string> motion_detection_file_path;
    r_utils::r_nullable<bool> do_motion_pruning;
    r_utils::r_nullable<int> min_continuous_recording_hours;

    std::string stream_config_hash;
};

}

#endif
