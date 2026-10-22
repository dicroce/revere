
#include "r_vss/r_storage_ops.h"
#include "r_vss/r_stream_keeper.h"
#include "r_disco/r_devices.h"
#include "r_storage/r_storage_file.h"
#include "r_storage/r_storage_file_reader.h"
#include "r_storage/r_ring.h"
#include "r_storage/r_md_storage_file.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_exception.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <utility>
#include <vector>

using namespace r_utils;
using namespace r_disco;
using namespace r_storage;
using namespace r_vss;
using namespace std;

string r_vss::storage_path(const string& top_dir, const string& file_path)
{
    // Check if it's already a full path (contains path separators)
    if(file_path.find('/') != string::npos || file_path.find('\\') != string::npos)
        return file_path;
    // Legacy format: just filename, prepend default video directory
    return top_dir + PATH_SLASH + "video" + PATH_SLASH + file_path;
}

void r_vss::create_motion_files(const string& motion_path)
{
    if(r_fs::file_exists(motion_path))
        return;
    r_ring::allocate(motion_path, 11, 2592000);          // 1 flag/sec, ~30 days
    r_md_storage_file::allocate(motion_path, 524288, 30); // 512KB blocks x 30
}

// Each removal is independent: a missing or locked file is logged and skipped,
// never fatal.
static void _remove_if_exists(const string& path)
{
    if(path.empty() || !r_fs::file_exists(path))
        return;
    try
    {
        r_fs::remove_file(path);
        R_LOG_INFO("Deleted camera storage file: %s", path.c_str());
    }
    catch(const std::exception& e)
    {
        R_LOG_ERROR("Failed to delete %s: %s", path.c_str(), e.what());
    }
}

// The main .nts plus the motion ring (.mdb), its sqlite sidecar (.db), and the
// analytics metadata store (.mdnts/.mdnts.db and its WAL/SHM journals).
static const char* MOTION_EXTS[] = {".mdb", ".db", ".mdnts", ".mdnts.db", ".mdnts.db-shm", ".mdnts.db-wal"};

static string _motion_base(const string& top_dir, const r_camera& camera)
{
    auto mdb = storage_path(top_dir, camera.motion_detection_file_path.value());
    return (mdb.size() >= 4 && mdb.substr(mdb.size() - 4) == ".mdb") ? mdb.substr(0, mdb.size() - 4) : mdb;
}

void r_vss::delete_camera_files(const string& top_dir, const r_camera& camera)
{
    if(!camera.record_file_path.is_null())
        _remove_if_exists(storage_path(top_dir, camera.record_file_path.value()));

    if(!camera.motion_detection_file_path.is_null())
    {
        auto base = _motion_base(top_dir, camera);
        for(auto ext : MOTION_EXTS)
            _remove_if_exists(base + ext);
    }
}

static string _join(const string& dir, const string& file_name)
{
    return dir + PATH_SLASH + file_name;
}

// The camera's files that exist, each with its file name.
static vector<pair<string, string>> _storage_file_pairs(const string& top_dir, const r_camera& camera)
{
    vector<pair<string, string>> files;

    if(!camera.record_file_path.is_null())
    {
        auto p = storage_path(top_dir, camera.record_file_path.value());
        if(r_fs::file_exists(p))
            files.push_back({p, std::filesystem::path(p).filename().string()});
    }

    if(!camera.motion_detection_file_path.is_null())
    {
        auto base = _motion_base(top_dir, camera);
        for(auto ext : MOTION_EXTS)
        {
            auto p = base + ext;
            if(r_fs::file_exists(p))
                files.push_back({p, std::filesystem::path(p).filename().string()});
        }
    }

    return files;
}

// Copy src to dst in chunks. progress_cb returns false to cancel.
static void _copy_file_chunked(const string& src, const string& dst, std::function<bool(uint64_t, uint64_t)> progress_cb)
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

void r_vss::storage_move(const string& top_dir, r_stream_keeper& stream_keeper, r_devices& devices, r_camera camera, const string& new_dir, r_storage_op& op)
{
    bool suspended = false;
    try
    {
        auto files = _storage_file_pairs(top_dir, camera);

        // Check for conflicts before touching anything
        for(auto& [src, fn] : files)
        {
            auto dst = _join(new_dir, fn);
            if(std::filesystem::exists(dst) && src != dst)
                R_THROW(("Destination file already exists: %s\nRemove it first or choose a different directory.", dst.c_str()));
        }

        stream_keeper.suspend(camera.id);
        suspended = true;

        if(files.empty())
        {
            op.set_status("No storage files found.");
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
            auto dst = _join(new_dir, fn);
            op.set_status("Moving: " + fn);

            std::error_code ec;
            std::filesystem::rename(src, dst, ec);
            if(ec)
            {
                // Cross-device: chunked copy so we can report progress
                uint64_t file_size = std::filesystem::file_size(src, ec);
                if(ec) file_size = 0;
                uint64_t bytes_done_before = bytes_done;

                _copy_file_chunked(src, dst, [&](uint64_t copied, uint64_t) -> bool {
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
                if(src == storage_path(top_dir, camera.record_file_path.value()))
                    new_nts_path = dst;
            if(!camera.motion_detection_file_path.is_null())
                if(src == storage_path(top_dir, camera.motion_detection_file_path.value()))
                    new_mdb_path = dst;
        }

        op.set_status("Updating database...");
        if(!new_nts_path.empty())
            camera.record_file_path.set_value(new_nts_path);
        if(!new_mdb_path.empty())
            camera.motion_detection_file_path.set_value(new_mdb_path);
        devices.save_camera(camera);

        op.set_status("Move complete. Recording will resume.");
        op.progress = 1.f;
        op.done = true;
        op.running = false;
        stream_keeper.resume(camera.id);
    }
    catch(const std::exception& e)
    {
        if(op.cancel_requested)
        {
            op.set_status("Move cancelled. Recording will resume.");
            op.cancel_requested = false;
        }
        else
        {
            op.set_status(string("Error: ") + e.what());
            op.error = true;
        }
        op.done = true;
        op.running = false;
        if(suspended)
            stream_keeper.resume(camera.id);
    }
}

void r_vss::storage_reset(const string& top_dir, r_stream_keeper& stream_keeper, r_devices& devices, r_camera camera, const string& new_dir, r_storage_op& op)
{
    try
    {
        stream_keeper.suspend(camera.id);

        op.set_status("Deleting old storage files...");
        delete_camera_files(top_dir, camera);
        op.progress = 0.3f;

        // Compute target directory
        string target_dir = new_dir;
        if(target_dir.empty() && !camera.record_file_path.is_null())
            target_dir = std::filesystem::path(storage_path(top_dir, camera.record_file_path.value())).parent_path().string();
        if(target_dir.empty())
        {
            target_dir = top_dir + PATH_SLASH + "video";
            r_fs::mkdir_p(target_dir);
        }

        string new_nts_path;
        string new_mdb_path;

        if(!camera.record_file_path.is_null())
            new_nts_path = _join(target_dir, std::filesystem::path(storage_path(top_dir, camera.record_file_path.value())).filename().string());
        if(!camera.motion_detection_file_path.is_null())
            new_mdb_path = _join(target_dir, std::filesystem::path(storage_path(top_dir, camera.motion_detection_file_path.value())).filename().string());

        op.set_status("Allocating new storage files...");

        if(!new_nts_path.empty() && !camera.n_record_file_blocks.is_null() && !camera.record_file_block_size.is_null())
            r_storage_file::allocate(new_nts_path, camera.record_file_block_size.value(), camera.n_record_file_blocks.value());
        op.progress = 0.7f;

        if(!new_mdb_path.empty())
            create_motion_files(new_mdb_path);
        op.progress = 0.9f;

        op.set_status("Updating database...");
        if(!new_nts_path.empty())
            camera.record_file_path.set_value(new_nts_path);
        if(!new_mdb_path.empty())
            camera.motion_detection_file_path.set_value(new_mdb_path);
        devices.save_camera(camera);

        op.set_status("Reset complete. Recording will resume.");
        op.progress = 1.f;
        op.done = true;
        op.running = false;
        stream_keeper.resume(camera.id);
    }
    catch(const std::exception& e)
    {
        op.set_status(string("Error: ") + e.what());
        op.error = true;
        op.running = false;
        stream_keeper.resume(camera.id);
    }
}

// Validates the file the way the recorder would (missing / truncated / bad
// magic all throw), takes block geometry from its header so retention math and
// any later reset use the real ring size, reuses a sibling .mdb if one exists
// (creates one otherwise when motion detection is on), then repoints the
// camera.
void r_vss::storage_locate(const string&, r_stream_keeper& stream_keeper, r_devices& devices, r_camera camera, const string& nts_path, r_storage_op& op)
{
    try
    {
        stream_keeper.suspend(camera.id);

        op.set_status("Checking recording file...");
        {
            // Throws r_not_found / generic on missing, truncated or non-nanots files.
            r_storage_file_reader reader(nts_path);
            (void)reader.last_ts();
        }
        op.progress = 0.4f;

        // Geometry straight from the nanots header (see nanots.h):
        //   8: block_size (u32)   12: n_blocks (u32)
        uint32_t block_size = 0, n_blocks = 0;
        {
            std::ifstream in(nts_path, std::ios::binary);
            char hdr[16] = {0};
            if(!in.read(hdr, sizeof(hdr)))
                R_THROW(("Unable to read header of %s", nts_path.c_str()));
            memcpy(&block_size, hdr + 8, 4);
            memcpy(&n_blocks, hdr + 12, 4);
        }
        if(block_size == 0)
            R_THROW(("%s does not look like a nanots recording file.", nts_path.c_str()));
        op.progress = 0.6f;

        auto dot = nts_path.find_last_of('.');
        auto mdb_path = (dot == string::npos) ? nts_path + ".mdb" : nts_path.substr(0, dot) + ".mdb";
        bool do_motion = !camera.do_motion_detection.is_null() && camera.do_motion_detection.value();
        if(do_motion && !r_fs::file_exists(mdb_path))
        {
            op.set_status("Creating motion files...");
            create_motion_files(mdb_path);
        }
        op.progress = 0.8f;

        op.set_status("Updating database...");
        camera.record_file_path.set_value(nts_path);
        camera.record_file_block_size.set_value(block_size);
        if(n_blocks > 0)
            camera.n_record_file_blocks.set_value(n_blocks);
        if(do_motion)
            camera.motion_detection_file_path.set_value(mdb_path);
        devices.save_camera(camera);

        op.set_status("Recording file attached. Recording will resume.");
        op.progress = 1.f;
        op.done = true;
        op.running = false;
        stream_keeper.resume(camera.id);
    }
    catch(const std::exception& e)
    {
        op.set_status(string("Error: ") + e.what());
        op.error = true;
        op.running = false;
        stream_keeper.resume(camera.id);
    }
}

void r_vss::storage_change_retention(const string& top_dir, r_stream_keeper& stream_keeper, r_devices& devices, r_camera camera, int64_t new_num_blocks, int64_t new_block_size, r_storage_op& op)
{
    try
    {
        if(new_num_blocks <= 0 || new_block_size <= 0)
            R_THROW(("Invalid retention size."));

        stream_keeper.suspend(camera.id);

        op.set_status("Deleting old recordings...");
        delete_camera_files(top_dir, camera);
        op.progress = 0.3f;

        string nts_path;
        string mdb_path;
        if(!camera.record_file_path.is_null())
            nts_path = storage_path(top_dir, camera.record_file_path.value());
        if(!camera.motion_detection_file_path.is_null())
            mdb_path = storage_path(top_dir, camera.motion_detection_file_path.value());

        op.set_status("Allocating new storage file...");
        if(!nts_path.empty())
            r_storage_file::allocate(nts_path, new_block_size, new_num_blocks);
        op.progress = 0.7f;

        if(!mdb_path.empty())
            create_motion_files(mdb_path);
        op.progress = 0.9f;

        op.set_status("Updating database...");
        camera.n_record_file_blocks.set_value(new_num_blocks);
        camera.record_file_block_size.set_value(new_block_size);
        devices.save_camera(camera);

        op.set_status("Retention updated. Recording will resume.");
        op.progress = 1.f;
        op.done = true;
        op.running = false;
        stream_keeper.resume(camera.id);
    }
    catch(const std::exception& e)
    {
        op.set_status(string("Error: ") + e.what());
        op.error = true;
        op.running = false;
        stream_keeper.resume(camera.id);
    }
}
