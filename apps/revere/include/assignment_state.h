
#ifndef __revere_assignment_state_h
#define __revere_assignment_state_h

#include <string>
#include "r_utils/r_nullable.h"
#include "r_disco/r_camera.h"
#include "r_pipeline/r_stream_info.h"
#include "r_ui_utils/texture.h"
#include <vector>
#include <memory>
#include <cstdint>
#include <map>

namespace revere
{

struct assignment_state
{
    std::string camera_id;
    std::string ipv4;
    r_utils::r_nullable<std::string> rtsp_username;
    r_utils::r_nullable<std::string> rtsp_password;
    std::string camera_friendly_name;
    int64_t byte_rate {64000};
    int continuous_retention_days {3};
    int motion_retention_days {10};
    int motion_percentage_estimate {5};
    std::string file_name;
    std::string storage_dir;  // Directory where recording files will be stored
    r_utils::r_nullable<int64_t> num_storage_file_blocks {0};
    r_utils::r_nullable<int64_t> storage_file_block_size {0};
    r_utils::r_nullable<r_disco::r_camera> camera;
    std::map<std::string, r_pipeline::r_sdp_media> sdp_medias;
    r_utils::r_nullable<std::shared_ptr<std::vector<uint8_t>>> maybe_key_frame;
    std::shared_ptr<r_ui_utils::texture> key_frame_texture;
    bool do_motion_detection {true};
    std::string motion_detection_file_path;
    r_utils::r_nullable<std::string> error_message;
};

}

#endif
