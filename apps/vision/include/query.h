
#ifndef __vision_query_h
#define __vision_query_h

#include <vector>
#include "imgui_ui.h"
#include "analytics_event.h"

namespace vision
{

std::vector<sidebar_list_ui_item> query_cameras(const std::string& ip_address);

std::vector<uint8_t> query_key(const std::string& ip_address, const std::string& camera_id, const std::string& start_time);

std::vector<segment> query_segments(const configure_state& cs, const std::string& camera_id, const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end);

std::vector<motion_event> query_motion_events(const configure_state& cs, const std::string& camera_id, const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end);

std::vector<analytics_event> query_analytics(const configure_state& cs, const std::string& camera_id, const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end, const std::string& stream_tag = "");

}

#endif
