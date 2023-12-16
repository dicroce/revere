
#ifndef __revere_ws_h
#define __revere_ws_h

#include "r_http/r_web_server.h"
#include "r_http/r_http_exception.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_macro.h"
#include "r_disco/r_devices.h"
#include "r_storage/r_storage_file.h"
#include <vector>
#include <chrono>

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
    std::chrono::system_clock::time_point first_ts;
    std::chrono::system_clock::time_point last_ts;
};

class r_ws final
{
public:
    R_API r_ws(const std::string& top_dir, r_disco::r_devices& devices);
    R_API ~r_ws();

    R_API std::vector<uint8_t> get_jpg(const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h);

    R_API std::chrono::hours get_retention_hours(const std::string& camera_id);

    R_API std::vector<uint8_t> get_key_frame(const std::string& camera_id, std::chrono::system_clock::time_point ts);

    R_API std::vector<uint8_t> get_video(const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

    R_API contents get_contents(const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

    R_API r_utils::r_nullable<std::chrono::system_clock::time_point> get_first_ts(const std::string& camera_id);

    R_API std::vector<r_disco::r_camera> get_cameras();

    R_API std::vector<motion_event_info> get_motion_events(const std::string& camera_id, uint8_t motion_threshold, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

    R_API std::vector<segment> get_blocks(const std::string& camera_id, std::chrono::system_clock::time_point start = {}, std::chrono::system_clock::time_point end = {});

    R_API void remove_blocks(const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

private:
    r_http::r_server_response _get_jpg(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                       r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                       const r_http::r_server_request& request);

    r_http::r_server_response _get_key_frame(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                             r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                             const r_http::r_server_request& request);

    r_http::r_server_response _get_contents(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                            r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                            const r_http::r_server_request& request);

    r_http::r_server_response _get_cameras(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                           r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                           const r_http::r_server_request& request);

    r_http::r_server_response _get_export(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                          r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                          const r_http::r_server_request& request);

    r_http::r_server_response _get_motions(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                           r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                           const r_http::r_server_request& request);

    r_http::r_server_response _get_motion_events(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                                 r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                                 const r_http::r_server_request& request);

    std::string _top_dir;
    r_disco::r_devices& _devices;
    r_http::r_web_server<r_utils::r_socket> _server;
};

#endif
