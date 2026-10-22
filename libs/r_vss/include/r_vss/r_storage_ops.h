#ifndef __r_vss_r_storage_ops_h
#define __r_vss_r_storage_ops_h

#include "r_disco/r_camera.h"
#include "r_utils/r_macro.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace r_disco { class r_devices; }

namespace r_vss
{

class r_stream_keeper;

// The operations on a camera's recording files that the desktop's Camera
// properties (Move storage, Change retention) and Repair run, shared with the
// web UI's endpoints. Each runs on the caller's thread, suspends the camera's
// recording while it works, reports progress and a status line through
// r_storage_op, and resumes recording when it ends, whether it succeeded,
// failed or was cancelled.
struct r_storage_op
{
    std::atomic<float> progress {0.f};
    std::atomic<bool> running {false};
    std::atomic<bool> done {false};
    std::atomic<bool> error {false};
    std::atomic<bool> cancel_requested {false};   // honored by the copy in storage_move
    std::mutex mtx;                               // guards status
    std::string status;

    void set_status(const std::string& s) { std::lock_guard<std::mutex> g(mtx); status = s; }
    std::string get_status() { std::lock_guard<std::mutex> g(mtx); return status; }
};

// A camera's storage path: record_file_path and motion_detection_file_path
// hold either a full path or, from older versions, a file name in
// <top_dir>/video.
R_API std::string storage_path(const std::string& top_dir, const std::string& file_path);

// The motion ring and analytics metadata files next to a recording; does
// nothing when motion_path exists.
R_API void create_motion_files(const std::string& motion_path);

// Deletes a camera's recording file and its motion files (.mdb and sidecars).
R_API void delete_camera_files(const std::string& top_dir, const r_disco::r_camera& camera);

// Moves the camera's files to new_dir (rename, or a chunked copy across
// filesystems, which cancel_requested stops) and points the camera at them.
R_API void storage_move(const std::string& top_dir, r_stream_keeper& stream_keeper, r_disco::r_devices& devices, r_disco::r_camera camera, const std::string& new_dir, r_storage_op& op);

// Deletes the camera's recordings and creates empty files of the same size in
// new_dir (the current folder when empty).
R_API void storage_reset(const std::string& top_dir, r_stream_keeper& stream_keeper, r_disco::r_devices& devices, r_disco::r_camera camera, const std::string& new_dir, r_storage_op& op);

// Adopts an existing recording file (Repair, when the camera's is missing).
R_API void storage_locate(const std::string& top_dir, r_stream_keeper& stream_keeper, r_disco::r_devices& devices, r_disco::r_camera camera, const std::string& nts_path, r_storage_op& op);

// Recreates the recording file with a new geometry (a new retention); the ring
// can't be resized in place, so the camera's recordings are deleted.
R_API void storage_change_retention(const std::string& top_dir, r_stream_keeper& stream_keeper, r_disco::r_devices& devices, r_disco::r_camera camera, int64_t new_num_blocks, int64_t new_block_size, r_storage_op& op);

}

#endif
