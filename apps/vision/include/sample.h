
#ifndef __vision_sample_h
#define __vision_sample_h

#include "r_pipeline/r_gst_buffer.h"
#include "r_pipeline/r_stream_info.h"

namespace vision
{

struct sample final
{
    r_pipeline::r_media media_type;
    r_pipeline::r_gst_buffer buffer;
    bool still {false};
    int64_t pts {0}; // absolute ms (stream start + sample pts); 0 for stills
};

}

#endif
