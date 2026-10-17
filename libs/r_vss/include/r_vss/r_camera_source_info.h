
#ifndef __r_vss_r_camera_source_info_h
#define __r_vss_r_camera_source_info_h

#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include <cstdint>
#include <string>
#include <utility>

namespace r_vss
{

// What a camera's last recorded key frame says about its stream: the coded
// resolution (from the SPS) and the framerate the recording context stamped
// into the codec parameters.
//
// r_camera::video_codec_parameters can't be used for this: those come from the
// discovery-time SDP and often lack sprop-parameter-sets for cameras that only
// send SPS/PPS inline. The parameters stored with each key frame are filled in
// from the running stream, so they're there for any camera that has recorded
// at least one key frame.
struct camera_source_info
{
    r_utils::r_nullable<std::pair<uint16_t, uint16_t>> resolution;
    r_utils::r_nullable<float> framerate;
};

// Reads the camera's recording file (its full path). Best-effort metadata:
// fields are null when the camera hasn't recorded yet, the codec isn't H.264
// or H.265, or the file can't be read. Never throws.
R_API camera_source_info read_camera_source_info(const std::string& storage_path);

}

#endif
