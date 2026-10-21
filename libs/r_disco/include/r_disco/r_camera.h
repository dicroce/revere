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
    // Where the ONVIF device service answers (xaddrs is only its path). Many
    // cameras use 80, but Reolinks use 8000. Null until discovery or setup finds it.
    r_utils::r_nullable<int> onvif_port;
    r_utils::r_nullable<std::string> onvif_protocol;
    r_utils::r_nullable<std::string> address;
    r_utils::r_nullable<std::string> rtsp_url;
    r_utils::r_nullable<std::string> video_codec;
    r_utils::r_nullable<std::string> video_codec_parameters;
    r_utils::r_nullable<int> video_timebase;
    r_utils::r_nullable<std::string> audio_codec;
    r_utils::r_nullable<std::string> audio_codec_parameters;
    r_utils::r_nullable<int> audio_timebase;

    // r_camera specific fields
    r_utils::r_nullable<std::string> onvif_profile_token;
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
    // Where motion and object classes come from (see r_vss::r_tier_controller):
    // "auto" (or null) lets Revere choose by verifying the camera's own events;
    // "1", "2" or "3" forces a tier.
    r_utils::r_nullable<std::string> motion_tier;
    // The tier automatic verification last chose (2 or 3), kept across
    // restarts; null when not verified.
    r_utils::r_nullable<int> motion_tier_verified;
    // RTSP transport to the camera: "TCP" (or null, the default) or "UDP".
    // With TCP, a camera that answers but won't stream over it falls back to
    // UDP automatically (see r_vss::r_rtsp_transport); UDP is used as is.
    r_utils::r_nullable<std::string> rtsp_transport;

    std::string stream_config_hash;
};

}

#endif
