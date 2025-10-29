#ifndef __test_plugin_h
#define __test_plugin_h

#include "r_vss/r_motion_plugin.h"
#include "r_utils/r_macro.h"

namespace r_vss {
    class r_motion_event_plugin_host;
}

class test_plugin : public r_vss::r_motion_plugin
{
public:
    R_API test_plugin(r_vss::r_motion_event_plugin_host* host);
    R_API virtual ~test_plugin();

    R_API virtual void post_motion_event(r_vss::r_motion_event evt, const std::string& camera_id, int64_t ts, const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height, const r_vss::motion_region& motion_bbox) override;

private:
    r_vss::r_motion_event_plugin_host* _host;
};

#endif