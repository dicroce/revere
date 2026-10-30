#ifndef __revere_query_h
#define __revere_query_h

#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include "r_disco/r_devices.h"
#include "r_storage/r_md_storage_file_reader.h"
#include "r_vss/r_display_key_frame.h"
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
    r_utils::r_nullable<std::chrono::system_clock::time_point> first_ts;
    r_utils::r_nullable<std::chrono::system_clock::time_point> last_ts;
};

struct measured_camera
{
    int64_t byte_rate;          // bytes/second measured off the live stream
    std::string video_codec;    // "h264" / "h265"
    std::vector<uint8_t> jpeg;  // small snapshot for the friendly-name dialog
};

// frame_ts, if given, gets the epoch-ms ts of the key frame decoded: the one
// r_storage_file_reader::query_key picks for ts. That isn't always at or before
// ts: before the first frame it's the first key frame.
R_API std::vector<uint8_t> query_get_jpg(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h, int64_t* frame_ts = nullptr);

// frame_ts as for query_get_jpg.
R_API std::vector<uint8_t> query_get_webp(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h, int64_t* frame_ts = nullptr);

R_API std::chrono::hours query_get_retention_hours(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id);

R_API std::vector<uint8_t> query_get_key_frame(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts);

// A key frame decoded the way YOLO takes it: RGB24, scaled to fit size x size
// with its aspect kept, centred on black (the motion engine's letterbox).
struct r_letterboxed_frame
{
    int64_t ts {0};             // the key frame's time
    std::vector<uint8_t> rgb;   // size * size * 3
    uint16_t size {640};
};

// Throws if the frame doesn't decode.
R_API r_letterboxed_frame decode_letterboxed_key_frame(const std::string& video_codec_name, const std::string& video_codec_parameters, const std::vector<uint8_t>& frame, uint16_t size = 640);

// The stored key frame at or before ts, letterboxed.
R_API r_letterboxed_frame query_get_letterboxed_key_frame(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t size = 640);

R_API std::vector<uint8_t> query_get_bgr24_frame(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h);

R_API std::vector<uint8_t> query_get_rgb24_frame(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h);

R_API std::vector<uint8_t> query_get_video(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

R_API contents query_get_contents(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

R_API r_utils::r_nullable<std::chrono::system_clock::time_point> query_get_first_ts(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id);

R_API std::vector<r_disco::r_camera> query_get_cameras(r_disco::r_devices& devices);

R_API std::vector<motion_event_info> query_get_motion_events(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

R_API std::vector<segment> query_get_blocks(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start = {}, std::chrono::system_clock::time_point end = {});

R_API void query_remove_blocks(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

R_API std::vector<r_storage::r_metadata_entry> query_get_analytics(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end, const r_utils::r_nullable<std::string>& stream_tag = r_utils::r_nullable<std::string>());

// Streams the (already-interrogated) camera's RTSP for a few seconds to measure
// its bitrate and grab a snapshot. The camera must already have rtsp_url and
// codec params populated (i.e. r_agent::interrogate_camera has run). Blocks for
// ~15s — callers run it off the request thread.
R_API measured_camera query_measure_camera(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, const r_utils::r_nullable<std::string>& username, const r_utils::r_nullable<std::string>& password, uint16_t w, uint16_t h);

// Allocates the recording (and, if requested, motion) storage files for a
// camera, writes its credentials/friendly-name/retention config, and flips it
// from "discovered" to "assigned" so the stream keeper begins recording. The
// camera must already be interrogated. byte_rate sizes the ring buffer.
R_API void query_configure_camera(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, const r_utils::r_nullable<std::string>& username, const r_utils::r_nullable<std::string>& password, const std::string& friendly_name, bool do_motion_detection, double retention_days, int64_t byte_rate);

// Stops recording a camera (unassigns it back to "discovered"). When
// delete_files is true, also waits for the stream keeper to release file
// handles and then deletes the camera's storage files (.nts and the motion
// .mdb/.db/.mdnts/.mdnts.db[-shm/-wal] set). Mirrors the desktop Remove dialog.
R_API void query_remove_camera(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, bool delete_files);

// Updates an assigned camera's recording settings (motion detection, motion
// pruning, minimum continuous retention hours) and persists them. When motion
// detection is enabled it ensures the camera's motion (.mdb/.mdnts) files exist,
// allocating them if missing. Mirrors the desktop Camera Properties dialog. The
// caller must bounce() the camera afterward for the change to take effect live.
// motion_tier: "auto", "1", "2" or "3" (see r_tier_controller); null leaves it as it is.
R_API void query_update_camera_properties(const std::string& top_dir, r_disco::r_devices& devices, const std::string& camera_id, bool do_motion_detection, bool do_motion_pruning, int min_continuous_recording_hours, const r_utils::r_nullable<std::string>& motion_tier = r_utils::r_nullable<std::string>());

}

#endif