
#ifndef __vision_stream_info_h
#define __vision_stream_info_h

#include <string>
#include "r_utils/r_nullable.h"
#include "layouts.h"

namespace vision
{

struct stream_info
{
    std::string name;
    std::string rtsp_url;
    std::string camera_id;
    bool do_motion_detection;
};

}

#endif
