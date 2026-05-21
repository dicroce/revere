
#ifndef __vision_query_h
#define __vision_query_h

#include <vector>
#include "imgui_ui.h"
#include "analytics_event.h"
#include "r_utils/r_nullable.h"

namespace vision
{

struct contents_result
{
    std::vector<segment> segments;
    r_utils::r_nullable<std::chrono::system_clock::time_point> first_ts;
    r_utils::r_nullable<std::chrono::system_clock::time_point> last_ts;
};

std::vector<sidebar_list_ui_item> query_cameras(const std::string& ip_address);

std::vector<uint8_t> query_key(const std::string& ip_address, const std::string& camera_id, const std::string& start_time);

contents_result query_segments(const configure_state& cs, const std::string& camera_id, const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end);

std::vector<motion_event> query_motion_events(const configure_state& cs, const std::string& camera_id, const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end);

std::vector<analytics_event> query_analytics(const configure_state& cs, const std::string& camera_id, const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end, const std::string& stream_tag = "");

int query_export_progress(const configure_state& cs, const std::string& export_id);

}

#endif
