
#ifndef __r_vss_r_motion_event_sink_h
#define __r_vss_r_motion_event_sink_h

#include "r_vss/r_motion_event.h"
#include "r_vss/r_motion_plugin.h"
#include <cstdint>
#include <string>
#include <vector>

namespace r_vss
{

class r_motion_event_sink
{
public:
    virtual ~r_motion_event_sink() = default;
    virtual void post(r_motion_event evt, const std::string& camera_id, int64_t ts,
                      const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height,
                      const motion_region& motion_bbox) = 0;
};

}

#endif
