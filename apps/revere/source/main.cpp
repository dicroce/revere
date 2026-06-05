
#include "r_utils/r_file.h"
#include "r_utils/r_file_lock.h"
#include <atomic>
#include <filesystem>

#ifdef IS_WINDOWS
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
#include <sys/file.h>
#include <unistd.h>
#include <signal.h>
#endif
#include <SDL.h>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_sdlrenderer.h"

#include <tray.hpp>

#include <string>
#include <thread>
#include <chrono>
#include <fstream>

#include "r_utils/r_string_utils.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_uuid.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_process.h"
#include "r_utils/r_args.h"
#include "r_utils/r_startup.h"
#include "r_utils/r_time_utils.h"
#include "r_disco/r_agent.h"
#include "r_disco/r_devices.h"
#include "r_disco/r_camera.h"
#include "r_vss/r_stream_keeper.h"
#include "r_vss/r_disk_benchmark.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_file.h"
#include "r_storage/r_md_storage_file.h"
#include "r_pipeline/r_stream_info.h"
#include "r_pipeline/r_gst_source.h"
#include "r_av/r_video_decoder.h"
#include "r_ui_utils/stb_image.h"
#include "r_ui_utils/font_catalog.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_ui_utils/texture_loader.h"
#include "r_ui_utils/wizard.h"

// Define the fonts global variable for this app
std::unordered_map<std::string, r_ui_utils::font_catalog> r_ui_utils::fonts;

#include "utils.h"
#include "gl_utils.h"

#include "imgui_ui.h"
#include "assignment_state.h"
#include "configure_state.h"
#include "rtsp_source_camera_config.h"

#include "R_32x32.h"
// R_32x32_png && R_32x32_png_len

#include "revere_version.h"

// Version info (generated at build time from git):
//   #include "revere_version.h"
//   REVERE_VERSION      - project version, e.g. "0.0.1"
//   REVERE_GIT_REVISION - short git hash, e.g. "a1b2c3d"
//   REVERE_GIT_DESCRIBE - git describe output, e.g. "v1.0.0-5-ga1b2c3d"
//
// https://github.com/dicroce/revere/releases/download/v0.0.1/revere-v0.0.1-x86_64-windows-setup.exe
//                                                            revere-v0.0.1-x86_64-linux.flatpak
//                                                            revere-v0.0.1-x86_64-macos.dmg
//                                                            revere_cloud-v0.0.1-x86_64-windows-setup.exe
//                                                            revere_cloud-v0.0.1-x86_64-linux.run
//                                                            revere_cloud-v0.0.1-x86_64-macos.command

using namespace std;
using namespace r_utils;
using namespace revere;

// Strip the [0] [1] [2]... backtrace lines that r_utils exceptions append to
// what() so the user-facing error modal shows only the human-readable cause.
static string _user_friendly_error(const std::exception& e)
{
    string msg = e.what();
    auto pos = msg.find('\n');
    if(pos == string::npos)
        return msg;
    // Trim any trailing whitespace/CR before the first newline.
    while(pos > 0 && (msg[pos-1] == '\r' || msg[pos-1] == ' ' || msg[pos-1] == '\t'))
        --pos;
    return msg.substr(0, pos);
}

// Format the per-camera retention string as "N of M days", where N is the
// current days on disk and M is the steady-state capacity of the ring file at
// the current bitrate (file_bytes / bytes_per_second). The wizard sizes the
// ring file to hold the user's requested days at the expected bitrate, so
// capacity is effectively the user's target — and reflects reality if the
// runtime bitrate diverges. Both numbers are floored; current is clamped at
// capacity so a transient bitrate dip doesn't render as e.g. "15 of 5 days".
static string _format_retention_days(const r_disco::r_camera& cam, int64_t bytes_per_second, double current_days)
{
    int64_t n_blocks = cam.n_record_file_blocks.is_null() ? 0 : cam.n_record_file_blocks.value();
    int64_t block_size = cam.record_file_block_size.is_null() ? 0 : cam.record_file_block_size.value();

    // No usable ring-size info or no bitrate yet — just show what's on disk.
    if(n_blocks <= 0 || block_size <= 0 || bytes_per_second <= 0)
        return r_string_utils::format("%lld days", (long long)current_days);

    int64_t file_bytes = n_blocks * block_size;
    double capacity_days = (double)file_bytes / (double)bytes_per_second / 86400.0;

    // Once current has caught up to capacity (steady-state, ring is full), the
    // "of M" half adds no information — both numbers track the same value as
    // bitrate jitters. Drop it. Note: current can legitimately exceed capacity
    // when video is sparse (low-motion stretches pack more wall-time per byte
    // than the average bitrate predicts), so don't clamp.
    if(current_days + 1.0 >= capacity_days)
        return r_string_utils::format("%lld days", (long long)current_days);

    return r_string_utils::format("%lld of %lld days",
        (long long)current_days,
        (long long)capacity_days);
}

struct revere_ui_state
{
    int recording_selected_item {-1};
    int discovered_selected_item {-1};
    vector<revere::sidebar_list_ui_item> recording_items;
    vector<revere::sidebar_list_ui_item> discovered_items;
    string friendly_name;
    string ipv4;
    string restream_url;
    string kbps;
    string retention;

    // camera properties dialog
    bool do_motion_detection {false};
    bool do_motion_pruning {false};
    std::string min_continuous_recording_hours {"24"};

    // camera removal dialog
    bool delete_camera_files {false};
    std::string camera_to_remove_name;

    bool minimize_requested {false};

    // Cached values for performance optimization
    float recording_largest_label {-1.0f};  // -1 means needs recalculation
    float discovered_largest_label {-1.0f}; // -1 means needs recalculation

    // Cached system plugins list (updated periodically, not per-frame)
    vector<string> loaded_system_plugins;
    void reset_selection()
    {
        recording_selected_item = -1;
        discovered_selected_item = -1;
        friendly_name.clear();
        ipv4.clear();
        restream_url.clear();
        kbps.clear();
        retention.clear();
    }
    bool recording_selected() const {return (recording_selected_item != -1)?true:false;}
    bool discovered_selected() const {return (discovered_selected_item != -1)?true:false;}
    r_nullable<string> selected_camera_id() const
    {
        r_nullable<string> output;
        if(recording_selected())
        {
            if(recording_selected_item != -1)
                output.set_value(recording_items[recording_selected_item].camera_id);
        }
        else
        {
            if(discovered_selected_item != -1)
                output.set_value(discovered_items[discovered_selected_item].camera_id);
        }
        return output;
    }
};

// Global SDL renderer pointer for texture operations
static SDL_Renderer* g_renderer = nullptr;

extern ImGuiContext *GImGui;

void _assign_camera(revere::assignment_state& as, r_disco::r_devices& devices)
{
    auto c = as.camera.value();

    c.rtsp_username = as.rtsp_username;
    c.rtsp_password = as.rtsp_password;
    if(!as.selected_profile_token.empty())
        c.onvif_profile_token = as.selected_profile_token;
    c.friendly_name = as.camera_friendly_name;
    c.record_file_path = as.file_name;
    if(!as.num_storage_file_blocks.is_null())
        c.n_record_file_blocks = as.num_storage_file_blocks.value();
    if(!as.storage_file_block_size.is_null())
        c.record_file_block_size = as.storage_file_block_size.value();
    c.do_motion_detection.set_value(as.do_motion_detection);
    c.motion_detection_file_path.set_value(as.motion_detection_file_path);

    devices.assign_camera(c);
}

static string _make_file_name(string name)
{
    replace(begin(name), end(name), ' ', '_');
    return name + ".nts";
}

void _update_list_ui(revere_ui_state& ui_state, r_disco::r_devices& devices, r_vss::r_stream_keeper& streamKeeper)
{
    // Use cached versions to avoid blocking the UI thread
    auto assigned_cameras = devices.get_assigned_cameras_cached();

    map<string, r_disco::r_camera> assigned;
    for(auto& c: assigned_cameras)
        assigned[c.id] = c;

    // Get stream status for all cameras to populate kbps and retention (already cached/non-blocking)
    auto stream_status = streamKeeper.fetch_stream_status();
    map<string, decltype(stream_status)::value_type> status_by_id;
    for(auto& status : stream_status)
        status_by_id[status.camera.id] = status;

    ui_state.recording_items.clear();
    for(auto& c : assigned)
    {
        revere::sidebar_list_ui_item item;
        item.label = c.second.friendly_name.value();
        item.sub_label = c.second.ipv4.value();
        item.camera_id = c.first;

        // Populate kbps, retention, and health if stream status is available
        if(status_by_id.find(c.first) != status_by_id.end())
        {
            auto& status = status_by_id[c.first];
            if(status.failed)
            {
                // Recording context construction is failing (e.g. legacy nanots v1
                // file). Show the cause in the card so the user has a hint about
                // what's wrong without digging through logs.
                item.kbps = "failed to start";
                item.retention = status.failure_message;
                item.health = revere::stream_health::error;
            }
            else
            {
                item.kbps = r_string_utils::format("%ld kbps", (status.bytes_per_second*8)/1024);
                auto retention_days = ((double)streamKeeper.get_retention_hours(status.camera.id).count()) / 24.0;
                item.retention = _format_retention_days(status.camera, status.bytes_per_second, retention_days);
                item.health = status.receiving_video ? revere::stream_health::healthy : revere::stream_health::error;
            }
        }
        else
        {
            item.kbps = "N/A";
            item.retention = "N/A";
            item.health = revere::stream_health::unknown;
        }

        ui_state.recording_items.push_back(item);
    }

    // Use cached version to avoid blocking the UI thread
    auto all_cameras = devices.get_all_cameras_cached();

    ui_state.discovered_items.clear();
    for(int i = 0; i < (int)all_cameras.size(); ++i)
    {
        auto c = all_cameras[i];

        if(assigned.find(c.id) != assigned.end())
            continue;

        revere::sidebar_list_ui_item item;
        item.label = c.camera_name.value();
        item.sub_label = c.ipv4.value();
        item.camera_id = c.id;
        ui_state.discovered_items.push_back(item);
    }

    // Invalidate cached text size calculations since lists changed
    ui_state.recording_largest_label = -1.0f;
    ui_state.discovered_largest_label = -1.0f;
}

void _create_motion_files(const string& motion_path)
{
    if(r_fs::file_exists(motion_path))
        return;

    r_storage::r_ring::allocate(motion_path, 11, 2592000);

    // Also allocate metadata storage for analytics (person detection, etc.)
    // Use 512KB blocks, 30 blocks total
    r_storage::r_md_storage_file::allocate(motion_path, 524288, 30);
}

// Helper to get storage file path - handles both legacy (filename only) and new (full path) formats
static std::string _get_storage_path(const std::string& record_file_path)
{
    // Check if it's already a full path (contains path separators)
    if(record_file_path.find('/') != std::string::npos || record_file_path.find('\\') != std::string::npos)
        return record_file_path;
    // Legacy format: just filename, prepend default video directory
    return revere::join_path(revere::sub_dir("video"), record_file_path);
}

void _delete_camera_files(const r_disco::r_camera& camera)
{
    // Delete the main storage file (.nts)
    if(!camera.record_file_path.is_null())
    {
        auto storage_path = _get_storage_path(camera.record_file_path.value());
        if(r_fs::file_exists(storage_path))
        {
            try
            {
                r_fs::remove_file(storage_path);
                R_LOG_INFO("Deleted camera storage file: %s", storage_path.c_str());
            }
            catch(const std::exception& e)
            {
                R_LOG_ERROR("Failed to delete storage file %s: %s", storage_path.c_str(), e.what());
            }
        }
    }

    // Delete motion detection files (.mdb and .mdnts)
    if(!camera.motion_detection_file_path.is_null())
    {
        auto motion_base_path = _get_storage_path(camera.motion_detection_file_path.value());

        // Delete .mdb file (ring buffer)
        if(r_fs::file_exists(motion_base_path))
        {
            try
            {
                r_fs::remove_file(motion_base_path);
                R_LOG_INFO("Deleted camera motion file: %s", motion_base_path.c_str());
            }
            catch(const std::exception& e)
            {
                R_LOG_ERROR("Failed to delete motion file %s: %s", motion_base_path.c_str(), e.what());
            }
        }

        // Delete .db file (SQLite database for ring buffer)
        auto mdb_db_path = motion_base_path;
        if(mdb_db_path.size() >= 4 && mdb_db_path.substr(mdb_db_path.size() - 4) == ".mdb")
        {
            mdb_db_path = mdb_db_path.substr(0, mdb_db_path.size() - 4) + ".db";
            if(r_fs::file_exists(mdb_db_path))
            {
                try
                {
                    r_fs::remove_file(mdb_db_path);
                    R_LOG_INFO("Deleted camera motion database file: %s", mdb_db_path.c_str());
                }
                catch(const std::exception& e)
                {
                    R_LOG_ERROR("Failed to delete motion database file %s: %s", mdb_db_path.c_str(), e.what());
                }
            }
        }

        // Delete .mdnts file (metadata storage)
        auto mdnts_path = motion_base_path;
        if(mdnts_path.size() >= 4 && mdnts_path.substr(mdnts_path.size() - 4) == ".mdb")
        {
            mdnts_path = mdnts_path.substr(0, mdnts_path.size() - 4) + ".mdnts";
            if(r_fs::file_exists(mdnts_path))
            {
                try
                {
                    r_fs::remove_file(mdnts_path);
                    R_LOG_INFO("Deleted camera metadata file: %s", mdnts_path.c_str());
                }
                catch(const std::exception& e)
                {
                    R_LOG_ERROR("Failed to delete metadata file %s: %s", mdnts_path.c_str(), e.what());
                }
            }

            // Delete .mdnts.db file (SQLite database for metadata storage)
            auto mdnts_db_path = mdnts_path + ".db";
            if(r_fs::file_exists(mdnts_db_path))
            {
                try
                {
                    r_fs::remove_file(mdnts_db_path);
                    R_LOG_INFO("Deleted camera metadata database file: %s", mdnts_db_path.c_str());
                }
                catch(const std::exception& e)
                {
                    R_LOG_ERROR("Failed to delete metadata database file %s: %s", mdnts_db_path.c_str(), e.what());
                }
            }

            // Delete SQLite WAL-mode journal files (-shm and -wal)
            auto mdnts_db_shm_path = mdnts_db_path + "-shm";
            if(r_fs::file_exists(mdnts_db_shm_path))
            {
                try
                {
                    r_fs::remove_file(mdnts_db_shm_path);
                    R_LOG_INFO("Deleted SQLite shared memory file: %s", mdnts_db_shm_path.c_str());
                }
                catch(const std::exception& e)
                {
                    R_LOG_ERROR("Failed to delete SQLite shm file %s: %s", mdnts_db_shm_path.c_str(), e.what());
                }
            }

            auto mdnts_db_wal_path = mdnts_db_path + "-wal";
            if(r_fs::file_exists(mdnts_db_wal_path))
            {
                try
                {
                    r_fs::remove_file(mdnts_db_wal_path);
                    R_LOG_INFO("Deleted SQLite WAL file: %s", mdnts_db_wal_path.c_str());
                }
                catch(const std::exception& e)
                {
                    R_LOG_ERROR("Failed to delete SQLite wal file %s: %s", mdnts_db_wal_path.c_str(), e.what());
                }
            }
        }
    }
}

struct storage_op_state
{
    std::string camera_id;
    std::string camera_name;
    std::string new_dir;
    std::atomic<float> progress{0.f};
    std::mutex mtx;
    std::string status;
    std::atomic<bool> running{false};
    std::atomic<bool> done{false};
    std::atomic<bool> error{false};
    std::atomic<bool> cancel_requested{false};
    bool start_fresh{false};
    bool delete_confirmed{false};
};

static storage_op_state s_storage_op;

static vector<pair<string,string>> _get_storage_file_pairs(const r_disco::r_camera& camera)
{
    vector<pair<string,string>> files;

    if(!camera.record_file_path.is_null())
    {
        auto p = _get_storage_path(camera.record_file_path.value());
        if(r_fs::file_exists(p))
        {
            auto fn = std::filesystem::path(p).filename().string();
            files.push_back({p, fn});
        }
    }

    if(!camera.motion_detection_file_path.is_null())
    {
        auto mdb = _get_storage_path(camera.motion_detection_file_path.value());
        auto base = (mdb.size() >= 4 && mdb.substr(mdb.size()-4) == ".mdb")
                    ? mdb.substr(0, mdb.size()-4) : mdb;
        for(const char* ext : {".mdb", ".db", ".mdnts", ".mdnts.db", ".mdnts.db-shm", ".mdnts.db-wal"})
        {
            auto p = base + ext;
            if(r_fs::file_exists(p))
            {
                auto fn = std::filesystem::path(p).filename().string();
                files.push_back({p, fn});
            }
        }
    }

    return files;
}

// Copy src to dst in chunks. progress_cb returns false to cancel.
static void _copy_file_chunked(const string& src, const string& dst,
                                std::function<bool(uint64_t, uint64_t)> progress_cb)
{
    constexpr size_t CHUNK = 4 * 1024 * 1024; // 4 MB
    auto file_size = std::filesystem::file_size(src);

    std::ifstream in(src, std::ios::binary);
    if(!in) R_THROW(("Failed to open %s for reading", src.c_str()));
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if(!out) R_THROW(("Failed to open %s for writing", dst.c_str()));

    vector<char> buf(CHUNK);
    uint64_t copied = 0;
    while(in)
    {
        in.read(buf.data(), CHUNK);
        auto n = in.gcount();
        if(n <= 0) break;
        out.write(buf.data(), n);
        if(!out) R_THROW(("Write error on %s", dst.c_str()));
        copied += n;
        if(!progress_cb(copied, file_size))
        {
            out.close();
            std::filesystem::remove(dst);
            return; // cancelled
        }
    }
}

static void _do_move_storage(
    r_vss::r_stream_keeper& stream_keeper,
    r_disco::r_devices& devices,
    r_disco::r_camera camera,
    const string& new_dir,
    storage_op_state& op
)
{
    auto set_status = [&](const string& s) {
        std::lock_guard<std::mutex> g(op.mtx);
        op.status = s;
    };

    bool suspended = false;
    try
    {
        auto files = _get_storage_file_pairs(camera);

        // Check for conflicts before touching anything
        for(auto& [src, fn] : files)
        {
            auto dst = revere::join_path(new_dir, fn);
            if(std::filesystem::exists(dst) && src != dst)
                R_THROW(("Destination file already exists: %s\nRemove it first or choose a different directory.", dst.c_str()));
        }

        stream_keeper.suspend(camera.id);
        suspended = true;

        if(files.empty())
        {
            set_status("No storage files found.");
            op.progress = 1.f;
            op.done = true;
            op.running = false;
            stream_keeper.resume(camera.id);
            return;
        }

        // Weight progress by file size so the big .nts drives the bar
        uint64_t total_bytes = 0;
        for(auto& [src, fn] : files)
        {
            std::error_code ec;
            auto sz = std::filesystem::file_size(src, ec);
            total_bytes += ec ? 0 : sz;
        }
        if(total_bytes == 0) total_bytes = 1;

        string new_nts_path;
        string new_mdb_path;
        uint64_t bytes_done = 0;

        for(auto& [src, fn] : files)
        {
            auto dst = revere::join_path(new_dir, fn);
            set_status("Moving: " + fn);

            std::error_code ec;
            std::filesystem::rename(src, dst, ec);
            if(ec)
            {
                // Cross-device: chunked copy so we can report progress
                uint64_t file_size = std::filesystem::file_size(src, ec);
                if(ec) file_size = 0;
                uint64_t bytes_done_before = bytes_done;

                _copy_file_chunked(src, dst, [&](uint64_t copied, uint64_t /*sz*/) -> bool {
                    op.progress = (float)(bytes_done_before + copied) / (float)total_bytes;
                    return !op.cancel_requested.load();
                });
                if(op.cancel_requested) R_THROW(("Cancelled"));
                std::filesystem::remove(src, ec);
                bytes_done += file_size;
            }
            else
            {
                // rename was instant (same filesystem)
                uint64_t sz = 0;
                auto fsz = std::filesystem::file_size(dst, ec);
                if(!ec) sz = fsz;
                bytes_done += sz;
            }

            op.progress = (float)bytes_done / (float)total_bytes;

            if(!camera.record_file_path.is_null())
                if(src == _get_storage_path(camera.record_file_path.value()))
                    new_nts_path = dst;
            if(!camera.motion_detection_file_path.is_null())
                if(src == _get_storage_path(camera.motion_detection_file_path.value()))
                    new_mdb_path = dst;
        }

        set_status("Updating database...");
        if(!new_nts_path.empty())
            camera.record_file_path.set_value(new_nts_path);
        if(!new_mdb_path.empty())
            camera.motion_detection_file_path.set_value(new_mdb_path);
        devices.save_camera(camera);

        set_status("Move complete. Recording will resume.");
        op.progress = 1.f;
        op.done = true;
        op.running = false;
        stream_keeper.resume(camera.id);
    }
    catch(const std::exception& e)
    {
        if(op.cancel_requested)
        {
            set_status("Move cancelled. Recording will resume.");
            op.cancel_requested = false;
        }
        else
        {
            set_status(string("Error: ") + e.what());
            op.error = true;
        }
        op.done = true;
        op.running = false;
        if(suspended)
            stream_keeper.resume(camera.id);
    }
}

static void _do_reset_storage(
    r_vss::r_stream_keeper& stream_keeper,
    r_disco::r_devices& devices,
    r_disco::r_camera camera,
    const string& new_dir,
    storage_op_state& op
)
{
    auto set_status = [&](const string& s) {
        std::lock_guard<std::mutex> g(op.mtx);
        op.status = s;
    };

    try
    {
        stream_keeper.suspend(camera.id);

        set_status("Deleting old storage files...");
        _delete_camera_files(camera);
        op.progress = 0.3f;

        // Compute target directory
        string target_dir = new_dir;
        if(target_dir.empty() && !camera.record_file_path.is_null())
            target_dir = std::filesystem::path(_get_storage_path(camera.record_file_path.value())).parent_path().string();
        if(target_dir.empty())
            target_dir = revere::sub_dir("video");

        string new_nts_path;
        string new_mdb_path;

        if(!camera.record_file_path.is_null())
        {
            auto fn = std::filesystem::path(_get_storage_path(camera.record_file_path.value())).filename().string();
            new_nts_path = revere::join_path(target_dir, fn);
        }
        if(!camera.motion_detection_file_path.is_null())
        {
            auto fn = std::filesystem::path(_get_storage_path(camera.motion_detection_file_path.value())).filename().string();
            new_mdb_path = revere::join_path(target_dir, fn);
        }

        set_status("Allocating new storage files...");

        if(!new_nts_path.empty() && !camera.n_record_file_blocks.is_null() && !camera.record_file_block_size.is_null())
        {
            r_storage::r_storage_file::allocate(
                new_nts_path,
                camera.record_file_block_size.value(),
                camera.n_record_file_blocks.value()
            );
        }
        op.progress = 0.7f;

        if(!new_mdb_path.empty())
            _create_motion_files(new_mdb_path);
        op.progress = 0.9f;

        set_status("Updating database...");
        if(!new_nts_path.empty())
            camera.record_file_path.set_value(new_nts_path);
        if(!new_mdb_path.empty())
            camera.motion_detection_file_path.set_value(new_mdb_path);
        devices.save_camera(camera);

        set_status("Reset complete. Recording will resume.");
        op.progress = 1.f;
        op.done = true;
        op.running = false;
        stream_keeper.resume(camera.id);
    }
    catch(const std::exception& e)
    {
        set_status(string("Error: ") + e.what());
        op.error = true;
        op.running = false;
        stream_keeper.resume(camera.id);
    }
}

void _on_new_file(revere::assignment_state& as, r_ui_utils::wizard& camera_setup_wizard, r_disco::r_devices& devices, revere_ui_state& ui_state, r_vss::r_stream_keeper& streamKeeper)
{
    // Use selected storage directory (defaults to standard video path)
    auto video_path = as.storage_dir.empty() ? revere::sub_dir("video") : as.storage_dir;

    auto c = as.camera.value();

    auto storage_path = revere::join_path(video_path, as.file_name);

    if(r_fs::file_exists(storage_path))
    {
        as.error_message = "Storage file already exists";
        camera_setup_wizard.next("error_modal");
        return;
    }

    uint64_t fs_size = 0;
    uint64_t fs_free = 0;
    r_fs::get_fs_usage(video_path, fs_size, fs_free);

    if(fs_free < (uint64_t)(as.storage_file_block_size.value() * as.num_storage_file_blocks.value()))
    {
        as.error_message = "Not enough free space on storage device.";
        camera_setup_wizard.next("error_modal");
        return;
    }

    if(as.do_motion_detection)
    {
        auto dot_pos = as.file_name.find_last_of('.');
        auto motion_file_name = (dot_pos == string::npos)?as.file_name+".mdb":as.file_name.substr(0, dot_pos)+".mdb";

        auto motion_path = revere::join_path(video_path, motion_file_name);

        _create_motion_files(motion_path);

        // Store motion file as full path
        as.motion_detection_file_path = motion_path;
    }
    
    camera_setup_wizard.cancel();

    r_storage::r_storage_file::allocate(storage_path, as.storage_file_block_size.value(), as.num_storage_file_blocks.value());

    // Store full path in file_name so it gets saved to database
    as.file_name = storage_path;

    _assign_camera(as, devices);

    _update_list_ui(ui_state, devices, streamKeeper);
}

static AVCodecID _r_encoding_to_avcodec_id(r_pipeline::r_encoding e)
{
    switch(e)
    {
    case r_pipeline::r_encoding::H264_ENCODING:
        return AV_CODEC_ID_H264;
    case r_pipeline::r_encoding::H265_ENCODING:
        return AV_CODEC_ID_HEVC;
    case r_pipeline::r_encoding::AAC_LATM_ENCODING:
        return AV_CODEC_ID_AAC_LATM;
    case r_pipeline::r_encoding::AAC_GENERIC_ENCODING:
        return AV_CODEC_ID_AAC;
    case r_pipeline::r_encoding::PCMU_ENCODING:
        return AV_CODEC_ID_PCM_MULAW;
    case r_pipeline::r_encoding::PCMA_ENCODING:
        return AV_CODEC_ID_PCM_ALAW;
    default:
        R_THROW(("Unknown encoding!"));
    }
}

static void _append_extradata(std::vector<uint8_t>& ed, const std::vector<uint8_t>& start_code, const std::string& base64_data)
{
    auto sprop_buffer = r_string_utils::from_base64(base64_data);
    auto current_size = ed.size();
    ed.resize(current_size + sprop_buffer.size() + start_code.size());
    memcpy(&ed[current_size], &start_code[0], start_code.size());
    memcpy(&ed[current_size + start_code.size()], sprop_buffer.data(), sprop_buffer.size());
}

static r_nullable<shared_ptr<vector<uint8_t>>> _decode_frame(const r_pipeline::sample_context& sample_ctx, const vector<uint8_t>& key_frame, uint16_t output_width, uint16_t output_height, AVPixelFormat fmt)
{
    auto video_enc = sample_ctx.video_encoding();
    if(video_enc.is_null())
        return r_nullable<shared_ptr<vector<uint8_t>>>();

    // Enable parsing to properly handle Annex B streams with multiple NAL units
    auto codec_id = _r_encoding_to_avcodec_id(video_enc.value());
    r_av::r_video_decoder decoder(codec_id, r_av::r_find_best_hw_accel(codec_id), true);

    std::vector<uint8_t> ed;
    std::vector<uint8_t> start_code = {0x00, 0x00, 0x00, 0x01};

    auto vps = sample_ctx.sprop_vps();
    auto sps = sample_ctx.sprop_sps();
    auto pps = sample_ctx.sprop_pps();

    if(!vps.is_null() && !vps.value().empty())
        _append_extradata(ed, start_code, vps.value());
    if(!sps.is_null() && !sps.value().empty())
        _append_extradata(ed, start_code, sps.value());
    if(!pps.is_null() && !pps.value().empty())
        _append_extradata(ed, start_code, pps.value());

    // Check if keyframe has inline SPS (NAL type 7)
    bool has_inline_sps = false;
    for(size_t i = 0; i + 4 < key_frame.size() && i < 500; ++i)
    {
        if(key_frame[i] == 0x00 && key_frame[i+1] == 0x00 &&
           key_frame[i+2] == 0x00 && key_frame[i+3] == 0x01)
        {
            uint8_t nal_type = key_frame[i+4] & 0x1F;
            if(nal_type == 7) // SPS
            {
                has_inline_sps = true;
                break;
            }
        }
    }

    // Only set extradata if stream doesn't have inline SPS/PPS
    // When parsing is enabled and stream has inline params, let the parser handle them
    if(ed.size() > 0 && !has_inline_sps)
        decoder.set_extradata(ed);

    decoder.attach_buffer(&key_frame[0], key_frame.size());

    int attempt = 0;
    r_av::r_codec_state state = r_av::R_CODEC_STATE_INITIALIZED;
    while(attempt < 10 && state != r_av::R_CODEC_STATE_HAS_OUTPUT && state != r_av::R_CODEC_STATE_AGAIN_HAS_OUTPUT)
    {
        state = decoder.decode();
        ++attempt;
    }

    // If decoder accepted data but hasn't produced output yet, flush to force output
    if(state == r_av::R_CODEC_STATE_HUNGRY)
    {
        while(attempt < 20 && state != r_av::R_CODEC_STATE_HAS_OUTPUT && state != r_av::R_CODEC_STATE_AGAIN_HAS_OUTPUT)
        {
            state = decoder.flush();
            ++attempt;
        }
    }

    r_nullable<shared_ptr<vector<uint8_t>>> output;
    if(state == r_av::R_CODEC_STATE_HAS_OUTPUT || state == r_av::R_CODEC_STATE_AGAIN_HAS_OUTPUT)
        output.set_value(decoder.get(fmt, output_width, output_height, 1));

    return output;
}

void configure_camera_setup_wizard(
    revere::assignment_state& as,
    revere::rtsp_source_camera_config& rscc,
    r_ui_utils::wizard& camera_setup_wizard,
    r_ui_utils::texture_loader& tl,
    r_disco::r_agent& agent,
    r_disco::r_devices& devices,
    r_vss::r_stream_keeper& stream_keeper,
    revere_ui_state& ui_state
)
{
    camera_setup_wizard.add_step(
        "error_modal",
        [&as, &camera_setup_wizard](){
            ImGui::OpenPopup("Error");
            revere::error_modal(
                GImGui,
                "Error",
                as.error_message.value(),
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "camera_credentials",
        [&](){
            as.wizard_step = 1;
            auto title = as.step_title("Camera Credentials");
            ImGui::OpenPopup(title.c_str());
            revere::rtsp_credentials_modal(
                GImGui,
                title,
                as.rtsp_username,
                as.rtsp_password,
                [&]() {
                    // First, unselect the discovered camera so that when we move it to recording
                    // we're not left with an index pointing at a UI list element that no longer exists.
                    ui_state.discovered_selected_item = -1;

                    auto camera = as.camera.value();
                    as.selected_profile_token = "";
                    as.selected_profile_idx = 0;
                    as.profiles_were_filtered = false;

                    std::thread th([&, camera](){
                        try
                        {
                            auto all_profiles = agent.get_camera_profiles(
                                camera.ipv4.value(),
                                camera.xaddrs.value(),
                                as.rtsp_username,
                                as.rtsp_password
                            );

                            as.available_profiles.clear();
                            for(auto& p : all_profiles)
                            {
                                if(p.encoding == "H264" || p.encoding == "H265")
                                    as.available_profiles.push_back(p);
                            }
                            as.profiles_were_filtered = as.available_profiles.size() < all_profiles.size();

                            if(as.available_profiles.empty())
                            {
                                std::string found;
                                for(auto& p : all_profiles)
                                    found += (found.empty() ? "" : ", ") + p.encoding;
                                R_THROW(("Camera has no supported stream profiles (found: " + found + ")."));
                            }

                            if(as.available_profiles.size() == 1)
                            {
                                as.selected_profile_token = as.available_profiles[0].token;
                                camera_setup_wizard.next("complete_interrogation");
                            }
                            else
                            {
                                if(camera_setup_wizard.active())
                                    camera_setup_wizard.next("profile_selection");
                            }
                        }
                        catch(const r_utils::r_unauthorized_exception&)
                        {
                            as.error_message = "Invalid Credentials";
                            camera_setup_wizard.next("error_modal");
                        }
                        catch(const std::exception& e)
                        {
                            as.error_message = _user_friendly_error(e);
                            camera_setup_wizard.next("error_modal");
                        }
                    });
                    th.detach();

                    camera_setup_wizard.next("please_wait");
                },
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "profile_selection",
        [&as, &camera_setup_wizard, &agent, &devices](){
            as.wizard_step = 3;
            auto title = as.step_title("Select Stream Profile");
            ImGui::OpenPopup(title.c_str());
            ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Always);
            if(ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("Choose the stream profile to record:");
                if(as.profiles_were_filtered)
                {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.0f, 1.0f), "Some profiles were removed because their codecs are not supported.");
                }
                ImGui::Spacing();

                for(int i = 0; i < (int)as.available_profiles.size(); ++i)
                {
                    auto& p = as.available_profiles[i];
                    auto label = p.encoding + "  " + std::to_string(p.width) + "x" + std::to_string(p.height);
                    ImGui::RadioButton(label.c_str(), &as.selected_profile_idx, i);
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if(ImGui::Button("Next", ImVec2(120, 0)))
                {
                    as.selected_profile_token = as.available_profiles[as.selected_profile_idx].token;
                    ImGui::CloseCurrentPopup();
                    camera_setup_wizard.next("complete_interrogation");
                }
                ImGui::SameLine();
                if(ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                    camera_setup_wizard.cancel();
                }
                ImGui::EndPopup();
            }
        }
    );
    camera_setup_wizard.add_step(
        "complete_interrogation",
        [&as, &camera_setup_wizard, &agent, &devices](){
            auto camera = as.camera.value();
            as.interrogation_phase.store(0);
            as.interrogation_start = std::chrono::steady_clock::now();
            std::thread th([&, camera](){
                try
                {
                    as.interrogation_phase.store(1);  // ONVIF interrogation
                    agent.interrogate_camera(
                        camera.camera_name.value(),
                        camera.ipv4.value(),
                        camera.xaddrs.value(),
                        camera.address.value(),
                        as.rtsp_username,
                        as.rtsp_password,
                        as.selected_profile_token
                    );
                    as.camera = devices.get_camera_by_id(as.camera_id);

                    as.interrogation_phase.store(2);  // RTSP probe
                    auto cp = r_pipeline::fetch_camera_params(as.camera.value().rtsp_url.value(), as.rtsp_username, as.rtsp_password);

                    if(cp.sdp_medias.empty() || cp.bytes_per_second == 0)
                        R_THROW(("Unable to communicate with camera."));

                    as.byte_rate = cp.bytes_per_second;
                    as.sdp_medias = cp.sdp_medias;

                    as.interrogation_phase.store(3);  // Decoding key frame
                    as.maybe_key_frame = _decode_frame(cp.sample_ctx, cp.video_key_frame, 320, 240, AV_PIX_FMT_BGRA);

                    as.key_frame_texture.reset();
                    if(!as.maybe_key_frame.is_null())
                    {
                        as.key_frame_texture = r_ui_utils::texture::create_from_rgb(
                            g_renderer,
                            as.maybe_key_frame.raw()->data(),
                            320,
                            240
                        );
                    }

                    as.interrogation_phase.store(4);  // Done
                    if(camera_setup_wizard.active())
                        camera_setup_wizard.next("friendly_name");
                }
                catch(const r_utils::r_unauthorized_exception&)
                {
                    as.error_message = "Invalid Credentials";
                    camera_setup_wizard.next("error_modal");
                }
                catch(const std::exception& e)
                {
                    as.error_message = _user_friendly_error(e);
                    camera_setup_wizard.next("error_modal");
                }
            });
            th.detach();
            camera_setup_wizard.next("please_wait");
        }
    );
    camera_setup_wizard.add_step(
        "friendly_name",
        [&as, &camera_setup_wizard](){
            as.wizard_step = 4;
            auto title = as.step_title("Camera Friendly Name");
            ImGui::OpenPopup(title.c_str());
            revere::friendly_name_modal(
                GImGui,
                title,
                as,
                as.camera_friendly_name,
                [&]() {
                    camera_setup_wizard.next("motion_detection");
                },
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "please_wait",
        [&as, &camera_setup_wizard](){
            as.wizard_step = 2;
            auto title = as.step_title("Please Wait");
            ImGui::OpenPopup(title.c_str());

            // Build a per-phase status line so the user can see what the
            // interrogation thread is actually doing (and how long it's taken).
            const char* phase_text = "Starting...";
            switch(as.interrogation_phase.load())
            {
                case 0: phase_text = "Starting..."; break;
                case 1: phase_text = "Querying camera (ONVIF)..."; break;
                case 2: phase_text = "Probing video stream (RTSP)..."; break;
                case 3: phase_text = "Decoding key frame..."; break;
                case 4: phase_text = "Done."; break;
            }
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - as.interrogation_start).count();
            auto status_line = r_string_utils::format("%s  (%llds)", phase_text, (long long)elapsed);

            revere::please_wait_modal(
                GImGui,
                title,
                status_line,
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "motion_detection",
        [&as, &camera_setup_wizard](){
            as.wizard_step = 5;
            auto title = as.step_title("Motion Detection");
            ImGui::OpenPopup(title.c_str());
            revere::motion_detection_modal(
                GImGui,
                title,
                as.do_motion_detection,
                [&](){camera_setup_wizard.next("new_or_existing");},
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "new_or_existing",
        [&as, &camera_setup_wizard, &devices, &ui_state](){
            as.wizard_step = 6;
            auto title = as.step_title("Storage");
            ImGui::OpenPopup(title.c_str());
            revere::new_or_existing_modal(
                GImGui,
                title,
                [&](){
                    // In flatpak/snap, skip storage location dialog - we can only write to the default location
                    const char* flatpak_id = getenv("FLATPAK_ID");
                    const char* snap_id = getenv("SNAP");
                    if(flatpak_id != nullptr || snap_id != nullptr)
                    {
                        as.storage_dir = revere::sub_dir("video");
                        as.wizard_total_steps = 8;
                        camera_setup_wizard.next("retention");
                    }
                    else
                    {
                        camera_setup_wizard.next("choose_storage_location");
                    }
                },
                [&](){
                    as.wizard_total_steps = 5;
                    camera_setup_wizard.next("choose_file");
                },
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "choose_storage_location",
        [&as, &camera_setup_wizard](){
            as.wizard_step = 7;
            auto default_path = revere::sub_dir("video");
            auto title = as.step_title("Storage Location");
            ImGui::OpenPopup(title.c_str());
            revere::storage_location_modal(
                GImGui,
                title,
                default_path,
                [&, default_path](){
                    // Use default location
                    as.storage_dir = default_path;
                    camera_setup_wizard.next("retention");
                },
                [&](){
                    // Choose custom location
                    camera_setup_wizard.next("choose_storage_dir");
                },
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "choose_storage_dir",
        [&as, &camera_setup_wizard](){
            // Default to standard video directory for the picker starting point
            if(as.storage_dir.empty())
                as.storage_dir = revere::sub_dir("video");

            // Only open if not already open to avoid resetting state while user edits path
            if(!ImGuiFileDialog::Instance()->IsOpened("ChooseDirDlgKey"))
                ImGuiFileDialog::Instance()->OpenDialog("ChooseDirDlgKey", "Choose Storage Location", nullptr, as.storage_dir, "", 1, nullptr, 0);
            if(ImGuiFileDialog::Instance()->Display("ChooseDirDlgKey", ImGuiWindowFlags_None, ImVec2(800, 350)))
            {
                if(ImGuiFileDialog::Instance()->IsOk())
                {
                    as.storage_dir = ImGuiFileDialog::Instance()->GetCurrentPath();
                    camera_setup_wizard.next("retention");
                }
                else
                {
                    camera_setup_wizard.cancel();
                }
                ImGuiFileDialog::Instance()->Close();
            }
        }
    );
    camera_setup_wizard.add_step(
        "choose_file",
        [&as, &camera_setup_wizard, &devices, &ui_state, &stream_keeper](){
            // Only open if not already open to avoid resetting state while user edits path
            if(!ImGuiFileDialog::Instance()->IsOpened("ChooseFileDlgKey"))
                ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".nts", revere::sub_dir("video"));
            if(ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey", ImGuiWindowFlags_None, ImVec2(800, 350)))
            {
                if(ImGuiFileDialog::Instance()->IsOk())
                {
                    // Store full path instead of just filename
                    std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                    as.file_name = filePathName;

                    auto dotPos = filePathName.find_last_of('.');
                    auto mdbPathName = (dotPos == std::string::npos) ? filePathName + ".mdb" : filePathName.substr(0, dotPos) + ".mdb";
                    if(r_fs::file_exists(mdbPathName))
                    {
                        as.do_motion_detection = true;
                        as.motion_detection_file_path = mdbPathName;  // Store full path
                    }

                    _assign_camera(as, devices);

                    ui_state.reset_selection();

                    _update_list_ui(ui_state, devices, stream_keeper);

                    camera_setup_wizard.cancel();
                }
                else camera_setup_wizard.cancel();

                ImGuiFileDialog::Instance()->Close();
            }
        }
    );
    camera_setup_wizard.add_step(
        "retention",
        [&as, &camera_setup_wizard](){
            as.wizard_step = as.wizard_total_steps - 1;
            auto title = as.step_title("Configure Retention");
            ImGui::OpenPopup(title.c_str());
            revere::retention_modal(
                GImGui,
                title,
                as,
                [&](){camera_setup_wizard.next("new_file_name");},
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "new_file_name",
        [&as, &camera_setup_wizard, &devices, &ui_state, &stream_keeper](){
            as.wizard_step = as.wizard_total_steps;
            as.file_name = _make_file_name(as.camera_friendly_name);
            auto title = as.step_title("New File Name");
            ImGui::OpenPopup(title.c_str());
            revere::new_file_name_modal(
                GImGui,
                title,
                as.file_name,
                [&](){_on_new_file(as, camera_setup_wizard, devices, ui_state, stream_keeper);},
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "minimize_to_tray",
        [&ui_state, &camera_setup_wizard](){
            ImGui::OpenPopup("Minimize to Tray");
            revere::minimize_to_tray_modal(
                GImGui,
                "Minimize to Tray",
                [&](){camera_setup_wizard.cancel(); ui_state.minimize_requested = true;},
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );

    camera_setup_wizard.add_step(
        "startup_enabled_notification",
        [&camera_setup_wizard](){
            ImGui::OpenPopup("Startup Enabled");
            revere::startup_enabled_modal(
                GImGui,
                "Startup Enabled",
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );

    camera_setup_wizard.add_step(
        "about_revere_cloud",
        [&camera_setup_wizard](){
            ImGui::OpenPopup("About Revere Cloud");
            revere::about_revere_cloud_modal(
                GImGui,
                "About Revere Cloud",
                [&](){
                    std::string version = REVERE_VERSION;
                    std::string base_url = "https://github.com/dicroce/revere/releases/download/v" + version + "/";
                    std::string filename;
#if defined(IS_WINDOWS)
                    filename = "revere-v" + version + "-windows-cloud-setup.exe";
#elif defined(IS_LINUX)
                    filename = "revere-v" + version + "-linux-cloud.run";
#elif defined(IS_MACOS)
                    filename = "revere-v" + version + "-macos-cloud.command";
#else
                    #error "Unsupported platform"
#endif
                    revere::open_url_in_browser(base_url + filename);
                    camera_setup_wizard.cancel();
                },
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );

    camera_setup_wizard.add_step(
        "configure_rtsp_source_camera",
        [&ui_state, &camera_setup_wizard, &rscc, &devices](){
            ImGui::OpenPopup("Configure RTSP Source Camera");
            revere::configure_rtsp_source_camera_modal(
                GImGui,
                "Configure RTSP Source Camera",
                rscc,
                [&](){
                    r_disco::r_camera c;
                    c.id = r_uuid::generate();
                    c.camera_name.set_value(rscc.camera_name);
                    c.ipv4.set_value(rscc.ipv4);
                    c.rtsp_url.set_value(rscc.rtsp_url);
                    if(rscc.rtsp_username.size() > 0)
                    {
                        c.rtsp_username.set_value(rscc.rtsp_username);
                        c.rtsp_password.set_value(rscc.rtsp_password);
                    }

                    devices.save_camera(c);

                    camera_setup_wizard.cancel();
                },
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );

    camera_setup_wizard.add_step(
        "camera_properties_modal",
        [&ui_state, &camera_setup_wizard, &rscc, &devices, &stream_keeper, &as](){

            // Get camera info to display record file path
            auto camera_id = ui_state.selected_camera_id();
            string record_path;
            if(!camera_id.is_null())
            {
                auto camera_opt = devices.get_camera_by_id(camera_id.value());
                if(!camera_opt.is_null() && !camera_opt.value().record_file_path.is_null())
                    record_path = _get_storage_path(camera_opt.value().record_file_path.value());
            }

            ImGui::OpenPopup("Camera Properties");
            revere::camera_properties_modal(
                GImGui,
                "Camera Properties",
                ui_state.do_motion_detection,
                ui_state.do_motion_pruning,
                ui_state.min_continuous_recording_hours,
                record_path,
                [&](){
                    // OK button
                    auto camera_id = ui_state.selected_camera_id();
                    auto camera = devices.get_camera_by_id(camera_id.value()).value();

                    string initial_md_file_name;
                    if(!camera.motion_detection_file_path.is_null())
                        initial_md_file_name = camera.motion_detection_file_path.value();

                    string motion_file_name = initial_md_file_name;
                    auto existing_path = _get_storage_path(motion_file_name);
                    if(motion_file_name.empty() || !r_fs::file_exists(existing_path))
                    {
                        motion_file_name = camera.record_file_path.value();

                        auto dot_pos = motion_file_name.find_last_of('.');

                        motion_file_name = (dot_pos == string::npos)?motion_file_name+".mdb":motion_file_name.substr(0, dot_pos)+".mdb";

                        auto motion_path = _get_storage_path(motion_file_name);

                        _create_motion_files(motion_path);
                    }

                    camera.do_motion_detection.set_value(ui_state.do_motion_detection);
                    camera.motion_detection_file_path.set_value(motion_file_name);
                    camera.do_motion_pruning.set_value(ui_state.do_motion_pruning);
                    camera.min_continuous_recording_hours.set_value(r_string_utils::s_to_int(ui_state.min_continuous_recording_hours));
                    devices.save_camera(camera);

                    stream_keeper.bounce(camera_id.value());

                    ui_state.do_motion_detection = false;
                    ui_state.do_motion_pruning = false;
                    ui_state.min_continuous_recording_hours = "24";

                    camera_setup_wizard.cancel();
                },
                [&](){
                    ui_state.do_motion_detection = false;
                    ui_state.do_motion_pruning = false;
                    ui_state.min_continuous_recording_hours = "24";

                    camera_setup_wizard.cancel();
                },
                [&](){
                    // Move Storage button
                    auto cid = ui_state.selected_camera_id();
                    if(!cid.is_null())
                    {
                        auto maybe_camera = devices.get_camera_by_id(cid.value());
                        if(!maybe_camera.is_null())
                        {
                            auto& c = maybe_camera.value();
                            s_storage_op.camera_id = c.id;
                            s_storage_op.camera_name = c.friendly_name.is_null() ? c.camera_name.value() : c.friendly_name.value();
                            s_storage_op.new_dir.clear();
                            s_storage_op.running = false;
                            s_storage_op.done = false;
                            s_storage_op.error = false;
                            s_storage_op.progress = 0.f;
                            s_storage_op.start_fresh = false;
                            s_storage_op.delete_confirmed = false;
                        }
                    }
                    camera_setup_wizard.next("move_storage_modal");
                }
            );
        }
    );

    camera_setup_wizard.add_step(
        "move_storage_modal",
        [&camera_setup_wizard, &devices, &stream_keeper](){
            ImGui::OpenPopup("Move Storage");

            string status_copy;
            {
                std::lock_guard<std::mutex> g(s_storage_op.mtx);
                status_copy = s_storage_op.status;
            }

            revere::move_storage_modal(
                GImGui,
                "Move Storage",
                s_storage_op.camera_name,
                s_storage_op.new_dir,
                s_storage_op.start_fresh,
                s_storage_op.delete_confirmed,
                s_storage_op.progress.load(),
                s_storage_op.running.load(),
                s_storage_op.done.load(),
                s_storage_op.error.load(),
                status_copy,
                [&](){
                    auto maybe_camera = devices.get_camera_by_id(s_storage_op.camera_id);
                    if(maybe_camera.is_null()) return;
                    s_storage_op.running = true;
                    s_storage_op.done = false;
                    s_storage_op.error = false;
                    s_storage_op.progress = 0.f;
                    s_storage_op.cancel_requested = false;
                    {
                        std::lock_guard<std::mutex> g(s_storage_op.mtx);
                        s_storage_op.status.clear();
                    }
                    auto camera = maybe_camera.value();
                    auto new_dir = s_storage_op.new_dir;
                    bool start_fresh = s_storage_op.start_fresh;
                    if(start_fresh)
                    {
                        std::thread([&stream_keeper, &devices, camera, new_dir](){
                            _do_reset_storage(stream_keeper, devices, camera, new_dir, s_storage_op);
                        }).detach();
                    }
                    else
                    {
                        std::thread([&stream_keeper, &devices, camera, new_dir](){
                            _do_move_storage(stream_keeper, devices, camera, new_dir, s_storage_op);
                        }).detach();
                    }
                },
                [&](){
                    s_storage_op.cancel_requested = false;
                    s_storage_op.new_dir.clear();
                    s_storage_op.running = false;
                    s_storage_op.done = false;
                    s_storage_op.error = false;
                    s_storage_op.progress = 0.f;
                    s_storage_op.start_fresh = false;
                    s_storage_op.delete_confirmed = false;
                    {
                        std::lock_guard<std::mutex> g(s_storage_op.mtx);
                        s_storage_op.status.clear();
                    }
                    camera_setup_wizard.cancel();
                },
                [&](){
                    // Cancel in-progress op: signal thread, then wait for done before closing
                    s_storage_op.cancel_requested = true;
                }
            );
        }
    );

    camera_setup_wizard.add_step(
        "remove_camera_modal",
        [&ui_state, &camera_setup_wizard, &devices](){
            ImGui::OpenPopup("Remove Camera");
            revere::remove_camera_modal(
                GImGui,
                "Remove Camera",
                ui_state.camera_to_remove_name,
                ui_state.delete_camera_files,
                [&](){
                    // Delete files callback
                    auto camera_id = ui_state.selected_camera_id();
                    if(!camera_id.is_null())
                    {
                        auto maybe_camera = devices.get_camera_by_id(camera_id.value());
                        if(!maybe_camera.is_null())
                        {
                            auto camera = maybe_camera.value();

                            // First, unassign the camera - this will trigger stream_keeper
                            // to stop recording and close all file handles
                            devices.unassign_camera(camera);

                            // Wait a bit to ensure all file handles are closed
                            // The stream_keeper's main loop polls every 2 seconds, so we need to wait
                            // for at least one iteration to process the unassignment
                            std::this_thread::sleep_for(std::chrono::milliseconds(3000));

                            // Now it's safe to delete the files
                            _delete_camera_files(camera);
                        }
                    }
                    ui_state.reset_selection();
                    ui_state.delete_camera_files = false;
                    camera_setup_wizard.cancel();
                },
                [&](){
                    // Keep files callback
                    auto camera_id = ui_state.selected_camera_id();
                    if(!camera_id.is_null())
                    {
                        auto maybe_camera = devices.get_camera_by_id(camera_id.value());
                        if(!maybe_camera.is_null())
                        {
                            auto camera = maybe_camera.value();
                            devices.unassign_camera(camera);
                        }
                    }
                    ui_state.reset_selection();
                    ui_state.delete_camera_files = false;
                    camera_setup_wizard.cancel();
                },
                [&](){
                    // Cancel callback
                    ui_state.delete_camera_files = false;
                    camera_setup_wizard.cancel();
                }
            );
        }
    );
}

string _get_icon_path()
{
#ifdef IS_WINDOWS
    return r_fs::path_join(
        r_fs::working_directory(),
        "R.ico"
    );
#endif

#ifdef IS_MACOS
    // On macOS, system tray icons are treated as templates by default (monochrome)
    // For now, just use the R.png from the working directory
    // TODO: Create a proper monochrome template icon for better macOS integration
    return r_fs::path_join(r_fs::working_directory(), "R.png");
#endif

#ifdef IS_LINUX
    // In Flatpak, use icon name (not path) for libappindicator compatibility
    const char* flatpak_id = getenv("FLATPAK_ID");
    if (flatpak_id != nullptr)
    {
        // Return just the icon name - libappindicator will find it in the icon theme
        return "io.github.dicroce.Revere";
    }

    // In Snap, use icon name (not path) for libappindicator compatibility
    const char* snap = getenv("SNAP");
    if (snap != nullptr)
    {
        // Return just the icon name - libappindicator will find it in the icon theme
        return "revere";
    }

    // Try AppImage-style icon path
    std::string rel_icon_path = "/../share/icons/hicolor/128x128/apps/revere.png";
    std::string full_icon_path = r_fs::path_join(r_fs::working_directory(), rel_icon_path);

    if (r_fs::file_exists(full_icon_path))
        return full_icon_path;

    // Fallback to local PNG icon
    return r_fs::path_join(r_fs::working_directory(), "R.png");
#endif
}

void _set_working_dir()
{
    // Set the current working directory to the directory of the executable
    auto exe_path = r_fs::current_exe_path();
    auto wd = exe_path.substr(0, exe_path.find_last_of(PATH_SLASH));
    R_LOG_INFO("wd: %s", wd.c_str());
    r_fs::change_working_directory(wd);
}

void _set_adaptive_window_size(SDL_Window* window)
{
    // Get display bounds (which excludes taskbars/menubars on some platforms)
    SDL_Rect display_bounds;
    if (SDL_GetDisplayUsableBounds(0, &display_bounds) != 0)
    {
        R_LOG_WARNING("Unable to detect display bounds, using default window size");
        return;
    }

    int monitor_x = display_bounds.x;
    int monitor_y = display_bounds.y;
    int monitor_width = display_bounds.w;
    int monitor_height = display_bounds.h;

    R_LOG_INFO("Monitor workarea: %dx%d at (%d, %d)", monitor_width, monitor_height, monitor_x, monitor_y);

    // Target window should be approximately 1/3 of monitor width and 1/3 of monitor height
    // On macOS, use a larger initial window size (75% instead of 1/3)
#ifdef IS_MACOS
    int target_width = (monitor_width * 3) / 4;
    int target_height = (monitor_height * 3) / 4;
#else
    int target_width = monitor_width / 3;
    int target_height = monitor_height / 3;
#endif

    // Ensure minimum usable size (640x360 is reasonable minimum)
    const int min_width = 640;
    const int min_height = 360;
    if (target_width < min_width || target_height < min_height)
    {
        R_LOG_WARNING("Calculated window size too small (%dx%d), using minimum", target_width, target_height);
        target_width = min_width;
        target_height = min_height;
    }

    // Ensure we don't exceed monitor workarea
    if (target_width > monitor_width)
        target_width = monitor_width;
    if (target_height > monitor_height)
        target_height = monitor_height;

    R_LOG_INFO("Setting adaptive window size: %dx%d", target_width, target_height);
    SDL_SetWindowSize(window, target_width, target_height);

    // Center window on monitor
    int window_x = monitor_x + (monitor_width - target_width) / 2;
    int window_y = monitor_y + (monitor_height - target_height) / 2;
    SDL_SetWindowPosition(window, window_x, window_y);
}

void _set_window_icon(SDL_Window* window)
{
    int x, y, channels;
    unsigned char* pixels = stbi_load_from_memory(R_32x32_png, R_32x32_png_len, &x, &y, &channels, 4);
    if (pixels)
    {
        // Create SDL surface from pixel data
        SDL_Surface* icon = SDL_CreateRGBSurfaceFrom(
            pixels, x, y, 32, x * 4,
            0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
        );
        if (icon)
        {
            SDL_SetWindowIcon(window, icon);
            SDL_FreeSurface(icon);
        }
        stbi_image_free(pixels);
    }
}

#if defined(IS_LINUX) || defined(IS_MACOS)
static std::atomic<bool> g_sigterm_received{false};
static void _sigterm_handler(int) { g_sigterm_received = true; }
#endif

#ifdef IS_WINDOWS
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR pCmdLine, int)
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
int main(int argc, char** argv)
#endif
{
    // Parse arguments early — needed for --quit before single-instance check
#ifdef IS_WINDOWS
    int argc;
    // Use GetCommandLineW() to get the FULL command line including exe path
    // pCmdLine only contains arguments without the exe name
    LPWSTR* argv_p = CommandLineToArgvW(GetCommandLineW(), &argc);
    vector<string> arg_storage;
    for(int i = 0; i < argc; i++)
        arg_storage.push_back(r_string_utils::convert_wide_string_to_multi_byte_string(argv_p[i]));
    argc = (int)arg_storage.size();
    vector<char*> argv(argc);
    for(int i = 0; i < argc; i++)
        argv[i] = const_cast<char*>(arg_storage[i].c_str());
#endif
    auto args = r_args::parse_arguments(argc, &argv[0]);

    auto maybe_quit = r_args::check_argument(args, "--quit");

    // Compute top_dir early for lock file path
    auto top_dir = revere::top_dir();
    auto lock_file_path = r_fs::platform_path(top_dir + "/revere.lock");

    if(maybe_quit)
    {
        // --quit mode: signal the running instance to exit and wait for it.
        // Used by the installer before replacing binaries.
#ifdef IS_WINDOWS
        // Check if an instance is running via the named mutex
        HANDLE hCheckMutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\RevereAppMutex");
        if(!hCheckMutex)
            return 0; // Not running

        // Signal the running instance via the named quit event
        HANDLE hQuitEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Global\\RevereQuitEvent");
        if(hQuitEvent)
        {
            SetEvent(hQuitEvent);
            CloseHandle(hQuitEvent);
        }

        // Wait up to 10s for the instance to exit (mutex is released on exit)
        DWORD wait_result = WaitForSingleObject(hCheckMutex, 10000);
        CloseHandle(hCheckMutex);

        if(wait_result == WAIT_TIMEOUT)
        {
            // Escalate: read PID from lock file and force-terminate
            int pid_fd = _open(lock_file_path.c_str(), _O_RDONLY);
            if(pid_fd >= 0)
            {
                char pid_buf[32] = {};
                _read(pid_fd, pid_buf, sizeof(pid_buf) - 1);
                _close(pid_fd);
                DWORD target_pid = (DWORD)atoi(pid_buf);
                if(target_pid > 0)
                {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, target_pid);
                    if(hProc)
                    {
                        TerminateProcess(hProc, 1);
                        CloseHandle(hProc);
                    }
                }
            }
            // Wait another 5s after force-terminate
            HANDLE hFinalMutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\RevereAppMutex");
            if(hFinalMutex)
            {
                WaitForSingleObject(hFinalMutex, 5000);
                CloseHandle(hFinalMutex);
            }
            return 1;
        }
        return 0;
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
        // Check if running via non-blocking flock
        int wait_fd = open(lock_file_path.c_str(), O_RDWR, 0600);
        if(wait_fd < 0)
            return 0; // No lock file — not running

        if(flock(wait_fd, LOCK_EX | LOCK_NB) == 0)
        {
            // Acquired immediately — not running
            flock(wait_fd, LOCK_UN);
            close(wait_fd);
            return 0;
        }

        // Read PID from lock file and send SIGTERM
        char pid_buf[32] = {};
        pread(wait_fd, pid_buf, sizeof(pid_buf) - 1, 0);
        pid_t target_pid = (pid_t)atoi(pid_buf);
        if(target_pid > 0)
            kill(target_pid, SIGTERM);

        // Wait up to 10s for exit using r_file_lock (blocking exclusive lock)
        bool exited = false;
        auto wait_thread = std::thread([&](){
            r_utils::r_file_lock wait_lock(wait_fd);
            wait_lock.lock();
            exited = true;
        });

        auto deadline = chrono::steady_clock::now() + chrono::seconds(10);
        while(!exited && chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(chrono::milliseconds(100));

        if(!exited)
        {
            // Escalate to SIGKILL
            if(target_pid > 0)
                kill(target_pid, SIGKILL);

            deadline = chrono::steady_clock::now() + chrono::seconds(5);
            while(!exited && chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(chrono::milliseconds(100));

            if(!exited)
            {
                wait_thread.detach();
                close(wait_fd);
                return 1;
            }
        }

        wait_thread.join();
        close(wait_fd);
        return 0;
#endif
    }

    // Single instance check: prevent multiple revere processes from running
#ifdef IS_WINDOWS
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\RevereAppMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(NULL, L"Revere is already running.", L"Revere", MB_OK | MB_ICONINFORMATION);
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }
    // Create the quit event so --quit can signal us
    HANDLE hQuitEvent = CreateEventW(NULL, FALSE, FALSE, L"Global\\RevereQuitEvent");
    // Write PID to lock file so --quit can force-terminate if needed
    int lock_fd = _open(lock_file_path.c_str(), _O_CREAT | _O_RDWR | _O_TRUNC, _S_IREAD | _S_IWRITE);
    if(lock_fd >= 0)
    {
        auto pid_str = std::to_string(GetCurrentProcessId());
        _write(lock_fd, pid_str.c_str(), (unsigned int)pid_str.size());
    }
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
    int lock_fd = open(lock_file_path.c_str(), O_CREAT | O_RDWR, 0600);
    if (lock_fd >= 0 && flock(lock_fd, LOCK_EX | LOCK_NB) != 0)
    {
        fprintf(stderr, "Revere is already running.\n");
        close(lock_fd);
        return 1;
    }
    // Write our PID so --quit can send us SIGTERM
    if(lock_fd >= 0)
    {
        auto pid_str = std::to_string(getpid());
        ftruncate(lock_fd, 0);
        write(lock_fd, pid_str.c_str(), pid_str.size());
    }
    // Hold the lock via r_file_lock for the process lifetime
    r_utils::r_file_lock process_lock(lock_fd);
    // Install SIGTERM handler for graceful shutdown triggered by --quit
    signal(SIGTERM, _sigterm_handler);
#endif

    r_logger::install_terminate();

    auto log_path = revere::sub_dir("logs");
    revere::sub_dir("config");  // Ensure config dir exists for plugins

    revere::configure_state cfg_state(r_fs::platform_path(top_dir));
    cfg_state.load();

    r_logger::install_logger(r_fs::platform_path(log_path), "revere_log_");

    // UI state needs to be created before we can register the log callback
    // We'll register it after creating ui_state

    // Debug: Log all parsed arguments
    R_LOG_INFO("argc = %d", argc);
    for(int i = 0; i < argc; ++i)
        R_LOG_INFO("argv[%d] = '%s'", i, argv[i]);
    R_LOG_INFO("Parsed %zu arguments:", args.size());
    for(size_t i = 0; i < args.size(); ++i)
        R_LOG_INFO("  args[%zu]: name='%s', value='%s'", i, args[i].name.c_str(), args[i].value.c_str());

    auto maybe_start_minimized = r_args::check_argument(args, "--start_minimized");

    std::string sim_mttf_str;
    uint32_t sim_mttf_seconds = 0;
    if(r_args::check_argument(args, "--sim_mttf_seconds", sim_mttf_str))
        sim_mttf_seconds = (uint32_t)std::stoul(sim_mttf_str);

    R_LOG_INFO("Starting Revere: start_minimized = %s", maybe_start_minimized ? "true" : "false");

    r_raw_socket::socket_startup();

    _set_working_dir();

    r_pipeline::gstreamer_init();

    // Setup Dear ImGui context early so we can pass it to stream keeper
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    r_disco::r_agent agent(r_fs::platform_path(top_dir));
    r_disco::r_devices devices(r_fs::platform_path(top_dir));
    r_vss::r_stream_keeper streamKeeper(devices, r_fs::platform_path(top_dir));
    streamKeeper.set_system_plugin_api_result_cb([&cfg_state](const std::string& name, const std::string& json) {
        if(name == "camera_answers")
        {
            try
            {
                auto j = nlohmann::json::parse(json);
                for(auto& [camera_id, answers_j] : j.items())
                {
                    std::map<int, std::string> answers;
                    for(auto& [num_str, answer] : answers_j.items())
                        answers[std::stoi(num_str)] = answer.get<std::string>();
                    cfg_state.set_camera_answers(camera_id, answers);
                }
            }
            catch(const std::exception& e)
            {
                R_LOG_ERROR("api_result \"camera_answers\" parse error: %s", e.what());
            }
        }
    });

    streamKeeper.set_motion_tuning_fn([&cfg_state](const std::string& camera_id) -> r_vss::r_motion_tuning {
        auto size = cfg_state.get_camera_answer(camera_id, 34);
        if(!size.has_value())
            return {};
        if(*size == "close")
            return {0.010, 25.0};
        if(*size == "distant" || *size == "mixed")
            return {0.001, 6.0};
        return {};  // "Medium" or unrecognized → defaults
    });

    if(sim_mttf_seconds > 0)
    {
        R_LOG_WARNING("*** SIMULATED FAILURE MODE: streams will die randomly near every %u seconds ***", sim_mttf_seconds);
        streamKeeper.set_sim_mttf_seconds(sim_mttf_seconds);
    }

    agent.set_stream_change_cb(bind(&r_disco::r_devices::insert_or_update_devices, &devices, placeholders::_1));
    agent.set_credential_cb(bind(&r_disco::r_devices::get_credentials, &devices, placeholders::_1));
    agent.set_is_recording_cb(bind(&r_vss::r_stream_keeper::is_recording, &streamKeeper, placeholders::_1));

    // Background re-interrogation pipeline: when ONVIF discovery sees an
    // assigned camera move (new IP after a router reboot etc.), r_agent uses
    // these to refresh the stored rtsp_url silently, or to raise a per-camera
    // alert when the change is too significant to auto-apply (codec/profile
    // changed) and the user needs to remove + re-add the camera.
    agent.set_camera_lookup_cb(bind(&r_disco::r_devices::get_camera_by_id, &devices, placeholders::_1));
    agent.set_save_camera_cb(bind(&r_disco::r_devices::save_camera, &devices, placeholders::_1));
    agent.set_camera_alert_cb([&devices](const std::string& id, const std::string& msg){
        if(msg.empty())
            devices.clear_camera_alert(id);
        else
            devices.set_camera_alert(id, msg);
    });

    // Note: streamKeeper, devices, and agent will be started after log callback is registered

    // Setup SDL2
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        R_LOG_ERROR("Unable to initialize SDL: %s", SDL_GetError());
        return 1;
    }

    // Allow display sleep - SDL2 prevents it by default
    SDL_EnableScreenSaver();

    // Create window with SDL2
    // If starting minimized, create window hidden to avoid showing it briefly
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (maybe_start_minimized)
    {
        window_flags |= SDL_WINDOW_HIDDEN;
        R_LOG_INFO("Creating window with SDL_WINDOW_HIDDEN flag");
    }

    SDL_Window* window = SDL_CreateWindow(
        "Revere",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        960, 540,
        window_flags
    );

    if (window == nullptr)
    {
        R_LOG_ERROR("Unable to create SDL window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Platform-specific renderer selection:
    // - Windows: Use hardware-accelerated (Direct3D) - no packaging concerns
    // - Linux: Use software renderer - for AppImage/Snap compatibility (avoids OpenGL)
    SDL_Renderer* renderer = nullptr;
#ifdef IS_WINDOWS
    // Windows: Use hardware acceleration (Direct3D, not OpenGL)
    // No PRESENTVSYNC: SDL_RenderPresent() with vsync can block indefinitely on old/buggy GPU drivers,
    // freezing the entire UI. Frame rate is naturally limited by SDL_WaitEventTimeout(100) in the loop.
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == nullptr)
    {
        R_LOG_ERROR("Hardware renderer failed: %s, falling back to software", SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    }
#else
    // Linux/macOS: Use software renderer to avoid OpenGL packaging issues
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == nullptr)
    {
        R_LOG_ERROR("Software renderer failed: %s, trying hardware", SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    }
#endif

    if (renderer == nullptr)
    {
        R_LOG_ERROR("Unable to create SDL renderer: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Store global renderer pointer for texture operations
    g_renderer = renderer;

#ifdef IS_MACOS
    // On macOS with high DPI displays, set logical size to match window size
    // This ensures SDL handles the 2x scaling properly for Retina displays
    int window_w, window_h;
    SDL_GetWindowSize(window, &window_w, &window_h);
    SDL_RenderSetLogicalSize(renderer, window_w, window_h);
    R_LOG_INFO("Set SDL logical size to %dx%d for Retina display support", window_w, window_h);
#endif

    // Set adaptive window size based on monitor resolution
    _set_adaptive_window_size(window);

    string vision_cmd = "vision";
#ifdef IS_WINDOWS
    vision_cmd = "vision.exe";
#elif defined(IS_MACOS)
    // On macOS, use 'open' command to launch the app bundle
    // Look for vision.app next to revere.app first, then fall back to /Applications
    {
        auto exe_dir = r_fs::working_directory();
        auto local_vision = exe_dir + "/../../../vision.app";
        if(r_fs::is_dir(local_vision))
            vision_cmd = "open -n " + local_vision;
        else
            vision_cmd = "open -n /Applications/Vision.app";
    }
#endif

    r_process vision_process(vision_cmd, true); // Use detached process

#if !defined(IS_MACOS)
    _set_window_icon(window);
#endif

    std::atomic<bool> close_requested{false};
    bool user_close_handled = false;

    R_LOG_INFO("wd=%s\n",r_fs::working_directory().c_str());

    auto icon_path = _get_icon_path();

    R_LOG_INFO("icon_path=%s\n",icon_path.c_str());

    Tray::Tray tray("Revere", icon_path);
    tray.addEntry(Tray::Button("Exit", [&]{
        close_requested = true;
    }));
    tray.addEntry(Tray::Button("Show", [&]{
        SDL_ShowWindow(window);
        SDL_RaiseWindow(window);
    }));
    tray.addEntry(Tray::Button("Hide", [&]{
        SDL_HideWindow(window);
    }));
    tray.addEntry(Tray::Button("Launch Vision", [&]{
        if(!vision_process.running())
            vision_process.start();
    }));

    // Configure Dear ImGui (context already created earlier)
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends for SDL2
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer_Init(renderer);

    // Platform-specific font sizes (macOS renders fonts larger, so we use smaller sizes)
#ifdef IS_MACOS
    const float FONT_SIZE_24 = 18.0f;
    const float FONT_SIZE_22 = 16.0f;
    const float FONT_SIZE_20 = 15.0f;
    const float FONT_SIZE_18 = 14.0f;
    const float FONT_SIZE_14 = 15.0f;
#else
    const float FONT_SIZE_24 = 24.0f;
    const float FONT_SIZE_22 = 22.0f;
    const float FONT_SIZE_20 = 20.0f;
    const float FONT_SIZE_18 = 18.0f;
    const float FONT_SIZE_14 = 16.0f;
#endif

    r_ui_utils::load_fonts(io, FONT_SIZE_24, r_ui_utils::fonts);
    r_ui_utils::load_fonts(io, FONT_SIZE_22, r_ui_utils::fonts);
    r_ui_utils::load_fonts(io, FONT_SIZE_20, r_ui_utils::fonts);
    r_ui_utils::load_fonts(io, FONT_SIZE_18, r_ui_utils::fonts);
    r_ui_utils::load_fonts(io, FONT_SIZE_14, r_ui_utils::fonts);

    // Generate font key strings for the loaded sizes
    auto FONT_KEY_24 = r_string_utils::float_to_s(FONT_SIZE_24, 2);
    auto FONT_KEY_22 = r_string_utils::float_to_s(FONT_SIZE_22, 2);
    auto FONT_KEY_20 = r_string_utils::float_to_s(FONT_SIZE_20, 2);
    auto FONT_KEY_18 = r_string_utils::float_to_s(FONT_SIZE_18, 2);
    auto FONT_KEY_14 = r_string_utils::float_to_s(FONT_SIZE_14, 2);

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    revere_ui_state ui_state;

    // Log startup banner
    R_LOG_INFO("========================================");
    R_LOG_INFO("Revere Video Surveillance System");
    R_LOG_INFO("Starting up...");
    R_LOG_INFO("========================================");

    // Benchmark disk write speed before starting recording services
    auto video_path = revere::sub_dir("video");
    uint64_t disk_bytes_per_second = r_vss::benchmark_disk_write_speed(video_path);

    // Start background services
    streamKeeper.start();
    devices.start();
    agent.start();

    r_ui_utils::texture_loader tl;
    tl.set_renderer(renderer);  // Set the SDL renderer for texture operations
    revere::assignment_state as;
    revere::rtsp_source_camera_config rscc;
    r_ui_utils::wizard camera_setup_wizard;
    configure_camera_setup_wizard(as, rscc, camera_setup_wizard, tl, agent, devices, streamKeeper, ui_state);

    _update_list_ui(ui_state, devices, streamKeeper);

    // Initialize cached system plugins list
    ui_state.loaded_system_plugins = streamKeeper.get_loaded_system_plugins();

    // Auto-select first camera in Recording list if any exist
    if(ui_state.recording_items.size() > 0)
    {
        ui_state.recording_selected_item = 0;
    }

    auto last_ui_update_ts = chrono::steady_clock::now();
    bool force_ui_update = false;


    bool window_visible = !maybe_start_minimized;  // Start hidden if minimized flag is set
    bool need_render = true;  // Render at least once on startup

    // Main loop
    while(!close_requested)
    {
        // Wait for events with timeout (event-driven rendering for low CPU usage)
        // Timeout allows periodic updates and tray pumping even when idle
        SDL_Event event;
        bool had_event = SDL_WaitEventTimeout(&event, 100);  // 100ms timeout

        // Always render on timeout to allow UI updates (tooltips, animations, etc.)
        if (!had_event)
            need_render = true;

        if (had_event)
        {
            need_render = true;
            do {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT)
                {
                    if (user_close_handled)
                    {
                        // User clicked X, the window close handler already showed the dialog.
                        user_close_handled = false;
                    }
                    else
                    {
                        // OS-initiated quit (system shutdown/restart). Actually quit.
                        close_requested = true;
                    }
                }
                if (event.type == SDL_WINDOWEVENT)
                {
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE &&
                        event.window.windowID == SDL_GetWindowID(window))
                    {
                        user_close_handled = true;
                        camera_setup_wizard.next("minimize_to_tray");
                    }
                    if (event.window.event == SDL_WINDOWEVENT_SHOWN)
                        window_visible = true;
                    if (event.window.event == SDL_WINDOWEVENT_HIDDEN)
                        window_visible = false;
                    if (event.window.event == SDL_WINDOWEVENT_MINIMIZED)
                        window_visible = false;
                    if (event.window.event == SDL_WINDOWEVENT_RESTORED)
                    {
                        window_visible = true;
                        need_render = true;
                    }
                    if (event.window.event == SDL_WINDOWEVENT_EXPOSED)
                        need_render = true;
#ifdef IS_MACOS
                    // On macOS, update logical size when window is resized for proper Retina scaling
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    {
                        int new_w, new_h;
                        SDL_GetWindowSize(window, &new_w, &new_h);
                        SDL_RenderSetLogicalSize(renderer, new_w, new_h);
                    }
#endif
                }
                if (event.type == SDL_RENDER_TARGETS_RESET || event.type == SDL_RENDER_DEVICE_RESET)
                {
                    R_LOG_WARNING("Render device reset - recreating GPU resources");
                    ImGui_ImplSDLRenderer_DestroyDeviceObjects();
                    ImGui_ImplSDLRenderer_CreateDeviceObjects();
                    force_ui_update = true;
                }
            } while (SDL_PollEvent(&event));
        }

        tray.pump();

        // Check for external shutdown signals (from --quit / installer)
#ifdef IS_WINDOWS
        if(hQuitEvent && WaitForSingleObject(hQuitEvent, 0) == WAIT_OBJECT_0)
        {
            close_requested = true;
        }
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
        if(g_sigterm_received)
        {
            close_requested = true;
        }
#endif

        if(maybe_start_minimized)
        {
            maybe_start_minimized = false;
            SDL_HideWindow(window);
            window_visible = false;
        }

        if(cfg_state.need_save())
            cfg_state.save();

        // Skip rendering when window is hidden
        if(!window_visible)
        {
            continue;
        }

        auto now = chrono::steady_clock::now();

        // Check if periodic data update is needed
        bool need_data_update = ((now - last_ui_update_ts) > chrono::seconds(5)) || force_ui_update;

        if(need_data_update)
        {
            last_ui_update_ts = now;
            force_ui_update = false;
            need_render = true;  // Data changed, need to render

            _update_list_ui(ui_state, devices, streamKeeper);

            // Update cached system plugins list (only every 5 seconds, not every frame)
            ui_state.loaded_system_plugins = streamKeeper.get_loaded_system_plugins();

            // Also update stream status for selected camera (only every 5 seconds, not every frame)
            auto maybe_camera_id = ui_state.selected_camera_id();
            if(!maybe_camera_id.is_null())
            {
                auto stream_status = streamKeeper.fetch_stream_status();

                for(const auto& status : stream_status)
                {
                    if(status.camera.id == maybe_camera_id.value())
                    {
                        ui_state.friendly_name = status.camera.friendly_name.value();
                        ui_state.ipv4 = status.camera.ipv4.value();
                        ui_state.restream_url = r_string_utils::format("rtsp://127.0.0.1:8554/%s",ui_state.friendly_name.c_str());
                        ui_state.kbps = r_string_utils::format("%ld kbps", (status.bytes_per_second*8)/1024);
                        auto retention_days = ((double)streamKeeper.get_retention_hours(status.camera.id).count()) / 24.0;
                        ui_state.retention = _format_retention_days(status.camera, status.bytes_per_second, retention_days);
                    }
                }
            }
        }

        // Skip rendering if nothing changed
        if (!need_render)
            continue;

        need_render = false;  // Reset for next iteration

        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        auto window_size = ImGui::GetIO().DisplaySize;
        auto window_width = (uint16_t)window_size.x;
        auto window_height = (uint16_t)window_size.y;

        ImGui::PushFont(r_ui_utils::fonts[FONT_KEY_24].roboto_regular);

        auto client_top = revere::main_menu(
            [&](){
                close_requested = true;
            },
            [&](){
                camera_setup_wizard.next("minimize_to_tray");
            },
            [&](){
                camera_setup_wizard.next("configure_rtsp_source_camera");
            },
            [&](){
                if(!vision_process.running())
                    vision_process.start();
            },
            [&](){
                camera_setup_wizard.next("about_revere_cloud");
            },
            [&]() -> bool {
                // Get current startup state
                return r_startup::is_autostart_enabled("Revere");
            },
            [&](bool enabled){
                // Set startup state
                auto exe_path = r_fs::current_exe_path();
                bool success = r_startup::set_autostart(enabled, "Revere", exe_path, "--start_minimized");

                // Show confirmation dialog only when enabling and operation succeeded
                if (enabled && success)
                {
                    camera_setup_wizard.next("startup_enabled_notification");
                }
            },
            [&](){
                revere::open_url_in_browser("http://localhost:8088");
            }
        );

        r_utils::r_nullable<std::string> main_status;

        // Check for queue overflow warnings and display in status bar
        auto overflow_flags = streamKeeper.get_current_overflow_flags();
        if(overflow_flags != r_vss::r_overflow_type::none)
        {
            std::string warning_msg = "WARNING: System under heavy load - ";
            if(r_vss::has_overflow(overflow_flags, r_vss::r_overflow_type::motion_detection))
                warning_msg += "Motion detection ";
            if(r_vss::has_overflow(overflow_flags, r_vss::r_overflow_type::live_restream))
                warning_msg += "Live restreaming ";
            warning_msg += "dropping frames (" + std::to_string(streamKeeper.get_total_dropped_frames()) + " total)";
            main_status.set_value(warning_msg);
        }
        else
        {
            main_status.set_value(string("Revere Running"));
        }

        main_layout(
            client_top,
            window_width,
            window_height,
            [&](uint16_t x, uint16_t y, uint16_t w, uint16_t h){
                revere::thirds(
                    x,
                    y,
                    w,
                    h,
                    "Discovered",
                    [&](uint16_t pw){
                        revere::sidebar_list(
                            GImGui,
                            as,
                            ui_state.discovered_selected_item,
                            pw,
                            ui_state.discovered_items,
                            "Record",
                            [&](int i){
                                as.camera_id = ui_state.discovered_items[i].camera_id;
                                as.camera = devices.get_camera_by_id(as.camera_id);
                                as.ipv4 = as.camera.value().ipv4.value();
                                as.wizard_step = 1;
                                as.wizard_total_steps = 9;

                                camera_setup_wizard.next("camera_credentials");
                            },
                            [&](int i){
                                ui_state.reset_selection();
                                ui_state.discovered_selected_item = i;
                                ui_state.recording_selected_item = -1;
                                force_ui_update = true;
                            },
                            true, // should we include a "forget" button?
                            [&](int i){
                                auto camera_id = ui_state.discovered_items[i].camera_id;
                                auto camera = devices.get_camera_by_id(camera_id);
                                if(!camera.is_null())
                                {
                                    devices.remove_camera(camera.value());
                                    agent.forget(camera_id);
                                    ui_state.reset_selection();
                                    force_ui_update = true;
                                }
                            },
                            false, // Dont include the properites button
                            [](int){},
                            ui_state.discovered_largest_label,
                            FONT_KEY_24,
                            FONT_KEY_22
                        );
                    },
                    "Recording",
                    [&](uint16_t pw){
                        revere::sidebar_list(
                            GImGui,
                            as,
                            ui_state.recording_selected_item,
                            pw,
                            ui_state.recording_items,
                            "Remove",
                            [&](int i){
                                // Set up the UI state for the remove dialog
                                ui_state.recording_selected_item = i;
                                ui_state.discovered_selected_item = -1;
                                force_ui_update = true;

                                auto maybe_c = devices.get_camera_by_id(ui_state.recording_items[i].camera_id);
                                if(!maybe_c.is_null())
                                {
                                    auto c = maybe_c.value();
                                    // Store camera name for display in the dialog
                                    ui_state.camera_to_remove_name = c.friendly_name.is_null() ?
                                        c.camera_name.value() : c.friendly_name.value();
                                    // Reset the delete files checkbox to false (unchecked by default)
                                    ui_state.delete_camera_files = false;
                                    // Show the removal confirmation dialog
                                    camera_setup_wizard.next("remove_camera_modal");
                                }

                                force_ui_update = true;
                            },
                            [&](int i){
                                ui_state.recording_selected_item = i;
                                ui_state.discovered_selected_item = -1;
                                force_ui_update = true;
                            },
                            false, // Dont include forget button
                            [](int){},
                            true, // Do include the properties button
                            [&](int i){
                                // first update the ui state
                                ui_state.recording_selected_item = i;
                                ui_state.discovered_selected_item = -1;
                                force_ui_update = true;

                                // Go to camera properties modal
                                auto camera = devices.get_camera_by_id(ui_state.selected_camera_id().value()).value();
                                ui_state.do_motion_detection = camera.do_motion_detection.value();
                                ui_state.do_motion_pruning = camera.do_motion_pruning.value();
                                ui_state.min_continuous_recording_hours = r_string_utils::int_to_s(camera.min_continuous_recording_hours.value());
                                camera_setup_wizard.next("camera_properties_modal");
                            },
                            ui_state.recording_largest_label,
                            FONT_KEY_24,
                            FONT_KEY_22
                        );
                    },
                    "Loaded System Plugins",
                    [&](uint16_t){
                        ImGui::PushFont(r_ui_utils::fonts[FONT_KEY_18].roboto_regular);

                        // Use cached plugins list instead of calling get_loaded_system_plugins() every frame
                        if (ui_state.loaded_system_plugins.empty())
                        {
                            ImGui::Text("No system plugins loaded");
                        }
                        else
                        {
                            for (const auto& plugin_name : ui_state.loaded_system_plugins)
                            {
                                bool is_enabled = streamKeeper.is_system_plugin_enabled(plugin_name);
                                if (ImGui::Checkbox(plugin_name.c_str(), &is_enabled))
                                {
                                    streamKeeper.set_system_plugin_enabled(plugin_name, is_enabled);
                                }

                                // Show status indicator if plugin supports it
                                auto status_json = streamKeeper.get_system_plugin_status(plugin_name);
                                if (!status_json.empty())
                                {
                                    try
                                    {
                                    auto sj = nlohmann::json::parse(status_json);
                                    auto label = sj.value("label", std::string("Unknown"));
                                    auto color_arr = sj.value("color", std::vector<int>{100, 100, 100});
                                    int r = color_arr.size() > 0 ? color_arr[0] : 100;
                                    int g = color_arr.size() > 1 ? color_arr[1] : 100;
                                    int b = color_arr.size() > 2 ? color_arr[2] : 100;
                                    ImU32 dot_color = IM_COL32(r, g, b, 255);

                                    ImGui::Indent(28.0f);
                                    auto cursor = ImGui::GetCursorScreenPos();
                                    float dot_radius = 5.0f;
                                    ImVec2 dot_center(cursor.x + dot_radius, cursor.y + ImGui::GetTextLineHeight() * 0.5f);
                                    ImGui::GetWindowDrawList()->AddCircleFilled(dot_center, dot_radius, dot_color);
                                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dot_radius * 2.0f + 6.0f);
                                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", label.c_str());

                                    // Show status message details (parsed from JSON)
                                    auto status_message = streamKeeper.get_system_plugin_status_message(plugin_name);
                                    if (!status_message.empty())
                                    {
                                        try
                                        {
                                            auto j = nlohmann::json::parse(status_message);
                                            if (j.contains("lines") && j["lines"].is_array())
                                            {
                                                for (const auto& line : j["lines"])
                                                {
                                                    auto text = line.value("text", std::string());
                                                    if (text.empty())
                                                        continue;

                                                    bool has_copy = line.contains("copy");
                                                    bool has_url = line.contains("url");

                                                    if (has_url)
                                                    {
                                                        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                                                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", text.c_str());
                                                        ImGui::PopTextWrapPos();

                                                        auto url_val = line["url"].get<std::string>();
                                                        ImGui::SameLine();
                                                        if (ImGui::SmallButton(("Open##" + text).c_str()))
                                                        {
#if defined(IS_MACOS) || defined(__APPLE__)
                                                            std::string open_cmd = "open \"" + url_val + "\"";
#elif defined(IS_LINUX)
                                                            std::string open_cmd = "xdg-open \"" + url_val + "\"";
#elif defined(IS_WINDOWS)
                                                            std::string open_cmd = "start \"\" \"" + url_val + "\"";
#endif
                                                            system(open_cmd.c_str());
                                                        }
                                                        ImGui::SameLine();
                                                        if (ImGui::SmallButton(("Copy URL##" + text).c_str()))
                                                            ImGui::SetClipboardText(url_val.c_str());
                                                    }
                                                    else
                                                    {
                                                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "%s", text.c_str());

                                                        if (has_copy)
                                                        {
                                                            auto copy_val = line["copy"].get<std::string>();
                                                            ImGui::SameLine();
                                                            if (ImGui::SmallButton(("Copy##" + text).c_str()))
                                                                ImGui::SetClipboardText(copy_val.c_str());
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        catch (...) {}
                                    }

                                    ImGui::Unindent(28.0f);
                                    }
                                    catch (...) {}
                                }
                            }
                        }

                        ImGui::PopFont();
                    }
                );

            },
            main_status,
            FONT_KEY_18
        );

        camera_setup_wizard();

        ImGui::PopFont();

        tl.work();

        if(ui_state.minimize_requested)
        {
            ui_state.minimize_requested = false;
            SDL_HideWindow(window);
            window_visible = false;
        }

        ImGui::Render();

        // Clear with background color
        SDL_SetRenderDrawColor(renderer,
            (Uint8)(clear_color.x * 255),
            (Uint8)(clear_color.y * 255),
            (Uint8)(clear_color.z * 255),
            (Uint8)(clear_color.w * 255));
        SDL_RenderClear(renderer);

        // Render ImGui
        ImGui_ImplSDLRenderer_RenderDrawData(ImGui::GetDrawData());

        SDL_RenderPresent(renderer);
    }

    tray.exit();

    // Cleanup — order matters:
    // 1. Stop background workers first so they stop touching SDL textures
    streamKeeper.stop();
    agent.stop();
    // Note: devices.stop() is called by its destructor AFTER streamKeeper is destroyed
    // This order is important because plugins may use devices during their shutdown

    // 2. Tear down ImGui and SDL after all workers are stopped
    ImGui_ImplSDLRenderer_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    r_pipeline::gstreamer_deinit();

    r_raw_socket::socket_cleanup();

    r_logger::uninstall_logger();

    // Release the process lock last — this unblocks any waiting --quit process,
    // signalling that we have fully shut down.
#ifdef IS_WINDOWS
    if(lock_fd >= 0) _close(lock_fd);
    if(hQuitEvent) CloseHandle(hQuitEvent);
    if(hMutex) CloseHandle(hMutex);
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
    if(lock_fd >= 0)
    {
        process_lock.unlock();
        close(lock_fd);
    }
#endif

    return 0;
}
