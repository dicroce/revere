#ifndef __revere_query_h
#define __revere_query_h

#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include "r_disco/r_devices.h"
#include "r_storage/r_md_storage_file_reader.h"
#include <vector>
#include <chrono>
#include <string>

namespace r_vss
{

struct motion_event_info
{
    std::chrono::system_clock::time_point start;
    std::chrono::system_clock::time_point end;
    uint8_t motion;
    uint8_t avg_motion;
    uint8_t stddev;
};

struct segment
{
    std::chrono::system_clock::time_point start;
    std::chrono::system_clock::time_point end;
};

struct contents
{
    std::vector<segment> segments;
};

R_API std::vector<uint8_t> query_get_jpg(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h);

R_API std::chrono::hours query_get_retention_hours(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id);

R_API std::vector<uint8_t> query_get_key_frame(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts);

R_API std::vector<uint8_t> query_get_bgr24_frame(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h);

R_API std::vector<uint8_t> query_get_rgb24_frame(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h);

R_API std::vector<uint8_t> query_get_video(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

R_API contents query_get_contents(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

R_API r_utils::r_nullable<std::chrono::system_clock::time_point> query_get_first_ts(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id);

R_API std::vector<r_disco::r_camera> query_get_cameras(r_disco::r_devices& devices);

R_API std::vector<motion_event_info> query_get_motion_events(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, uint8_t motion_threshold, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

R_API std::vector<segment> query_get_blocks(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start = {}, std::chrono::system_clock::time_point end = {});

R_API void query_remove_blocks(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

struct motion_data_point
{
    std::chrono::system_clock::time_point time;
    uint8_t motion;
    uint8_t avg_motion;
    uint8_t stddev;
};

R_API std::vector<motion_data_point> query_get_motions(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, uint8_t motion_threshold, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

R_API std::vector<r_storage::r_metadata_entry> query_get_analytics(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end, const r_utils::r_nullable<std::string>& stream_tag = r_utils::r_nullable<std::string>());

}

#endif