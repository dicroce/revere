
#include "r_disco/r_devices.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_functional.h"
#include "r_utils/r_secure_store.h"
#include "r_utils/r_credential_crypto.h"
#include "r_utils/r_socket.h"
#include <thread>
#include <algorithm>
#include <iterator>
#include <chrono>
#include <set>

using namespace r_disco;
using namespace r_db;
using namespace r_utils;
using namespace r_utils::r_string_utils;
using namespace r_utils::r_funky;
using namespace std;

r_devices::r_devices(const string& top_dir) :
    _th(),
    _running(false),
    _top_dir(top_dir),
    _master_key(),
    _master_key_loaded(false)
{
}

r_devices::~r_devices() noexcept
{
    stop();
}

void r_devices::start()
{
    _running = true;
    _th = thread(&r_devices::_entry_point, this);
}

void r_devices::stop()
{
    if(_running)
    {
        _running = false;
        _db_work_q.wake();
        _th.join();
    }
}

void r_devices::insert_or_update_devices(const vector<pair<r_stream_config, string>>& stream_configs)
{
    try
    {
        r_devices_cmd cmd;
        cmd.type = INSERT_OR_UPDATE_DEVICES;
        cmd.configs = stream_configs;

        _db_work_q.post(cmd);
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }    
}

r_nullable<r_camera> r_devices::get_camera_by_id(const string& id)
{
    r_nullable<r_camera> camera;

    try
    {
        r_devices_cmd cmd;
        cmd.type = GET_CAMERA_BY_ID;
        cmd.id = id;

        auto result = _db_work_q.post(cmd).get();

        if(!result.cameras.empty())
            camera = result.cameras.front();
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }

    return camera;
}

vector<r_camera> r_devices::get_all_cameras()
{
    vector<r_camera> cameras;

    try
    {
        r_devices_cmd cmd;
        cmd.type = GET_ALL_CAMERAS;
        cameras = _db_work_q.post(cmd).get().cameras;
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }

    return cameras;
}

vector<r_camera> r_devices::get_assigned_cameras()
{
    vector<r_camera> cameras;

    try
    {
        r_devices_cmd cmd;
        cmd.type = GET_ASSIGNED_CAMERAS;
        cameras = _db_work_q.post(cmd).get().cameras;
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }

    return cameras;
}

vector<r_camera> r_devices::get_all_cameras_cached() const
{
    lock_guard<mutex> g(_cache_mutex);
    return _all_cameras_cache;
}

vector<r_camera> r_devices::get_assigned_cameras_cached() const
{
    lock_guard<mutex> g(_cache_mutex);
    return _assigned_cameras_cache;
}

void r_devices::save_camera(const r_camera& camera)
{
    try
    {
        r_devices_cmd cmd;
        cmd.type = SAVE_CAMERA;
        cmd.cameras.push_back(camera);
        _db_work_q.post(cmd).wait();
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }
}

void r_devices::remove_camera(const r_camera& camera)
{
    try
    {
        r_devices_cmd cmd;
        cmd.type = REMOVE_CAMERA;
        cmd.cameras.push_back(camera);
        _db_work_q.post(cmd).wait();
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }
}

void r_devices::assign_camera(r_camera& camera)
{
    camera.state = "assigned";
    save_camera(camera);
}

void r_devices::unassign_camera(r_camera& camera)
{
    camera.state = "discovered";
    save_camera(camera);
}

pair<r_nullable<string>, r_nullable<string>> r_devices::get_credentials(const std::string& id)
{
    pair<r_nullable<string>, r_nullable<string>> result;
    try
    {
        r_devices_cmd cmd;
        cmd.type = GET_CREDENTIALS_BY_ID;
        cmd.id = id;
        result = _db_work_q.post(cmd).get().credentials;
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }

    return result;
}

void r_devices::set_camera_alert(const std::string& camera_id, const std::string& message)
{
    lock_guard<mutex> g(_camera_alerts_mutex);
    _camera_alerts[camera_id] = message;
}

void r_devices::set_camera_move(const std::string& camera_id, const r_camera_move& move)
{
    lock_guard<mutex> g(_camera_moves_mutex);
    _camera_moves[camera_id] = move;
}

void r_devices::clear_camera_move(const std::string& camera_id)
{
    lock_guard<mutex> g(_camera_moves_mutex);
    _camera_moves.erase(camera_id);
}

r_nullable<r_camera_move> r_devices::get_camera_move(const std::string& camera_id) const
{
    lock_guard<mutex> g(_camera_moves_mutex);
    auto it = _camera_moves.find(camera_id);
    r_nullable<r_camera_move> result;
    if(it != _camera_moves.end())
        result.set_value(it->second);
    return result;
}

void r_devices::clear_camera_alert(const std::string& camera_id)
{
    lock_guard<mutex> g(_camera_alerts_mutex);
    _camera_alerts.erase(camera_id);
}

r_nullable<string> r_devices::get_camera_alert(const std::string& camera_id) const
{
    lock_guard<mutex> g(_camera_alerts_mutex);
    auto it = _camera_alerts.find(camera_id);
    if(it == _camera_alerts.end())
        return r_nullable<string>();
    r_nullable<string> result;
    result.set_value(it->second);
    return result;
}

vector<r_camera> r_devices::get_modified_cameras(const vector<r_camera>& cameras)
{
    vector<r_camera> modified_cameras;
    try
    {
        r_devices_cmd cmd;
        cmd.type = GET_MODIFIED_CAMERAS;
        cmd.cameras = cameras;
        modified_cameras = _db_work_q.post(cmd).get().cameras;
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }

    return modified_cameras;
}

vector<r_camera> r_devices::get_assigned_cameras_added(const vector<r_camera>& cameras)
{
    vector<r_camera> added_cameras;
    try
    {
        r_devices_cmd cmd;
        cmd.type = GET_ASSIGNED_CAMERAS_ADDED;
        cmd.cameras = cameras;
        added_cameras = _db_work_q.post(cmd).get().cameras;
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }

    return added_cameras;
}

vector<r_camera> r_devices::get_assigned_cameras_removed(const vector<r_camera>& cameras)
{
    vector<r_camera> removed_cameras;
    try
    {
        r_devices_cmd cmd;
        cmd.type = GET_ASSIGNED_CAMERAS_REMOVED;
        cmd.cameras = cameras;
        removed_cameras = _db_work_q.post(cmd).get().cameras;
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }

    return removed_cameras;
}

void r_devices::_update_caches()
{
    try
    {
        auto conn = _open_db(_top_dir, false);

        auto all_result = _get_all_cameras(conn);
        auto assigned_result = _get_assigned_cameras(conn);

        {
            lock_guard<mutex> g(_cache_mutex);
            _all_cameras_cache = std::move(all_result.cameras);
            _assigned_cameras_cache = std::move(assigned_result.cameras);
        }
        ++_cache_generation;
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }
}

uint64_t r_devices::cache_generation() const
{
    return _cache_generation.load();
}

void r_devices::_entry_point()
{
    // A failure here is logged, not fatal: it's retried, and the UI shows its
    // loading state meanwhile. Retries back off from 2 seconds to 5 minutes, so
    // a failure that repeats doesn't fill the log.
    bool db_created = false;
    auto retry_delay = chrono::seconds(2);
    auto next_create = chrono::steady_clock::now();
    auto create_db = [&]() {
        try
        {
            _create_db(_top_dir);
            db_created = true;
        }
        catch(const std::exception& e)
        {
            R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
            next_create = chrono::steady_clock::now() + retry_delay;
            retry_delay = (std::min)(retry_delay * 2, chrono::seconds(chrono::minutes(5)));
        }
    };
    create_db();

    // Initialize caches immediately on startup
    _update_caches();

    while(_running)
    {
        auto maybe_cmd = _db_work_q.poll(chrono::seconds(2));
        if(!maybe_cmd.is_null())
        {
            auto cmd = maybe_cmd.take();

            try
            {
                // XXX how about open for read only for the read only operations?

                if(cmd.first.type == INSERT_OR_UPDATE_DEVICES)
                {
                    auto conn = _open_db(_top_dir);
                    cmd.second.set_value(_insert_or_update_devices(conn, cmd.first.configs));
                }
                else if(cmd.first.type == GET_CAMERA_BY_ID)
                {
                    auto conn = _open_db(_top_dir);
                    cmd.second.set_value(_get_camera_by_id(conn, cmd.first.id));
                }
                else if(cmd.first.type == GET_ALL_CAMERAS)
                {
                    auto conn = _open_db(_top_dir);
                    cmd.second.set_value(_get_all_cameras(conn));
                }
                else if(cmd.first.type == GET_ASSIGNED_CAMERAS)
                {
                    auto conn = _open_db(_top_dir);
                    cmd.second.set_value(_get_assigned_cameras(conn));
                }
                else if(cmd.first.type == SAVE_CAMERA)
                {
                    auto conn = _open_db(_top_dir);
                    if(cmd.first.cameras.empty())
                        R_THROW(("No cameras passed to SAVE_CAMERA."));
                    cmd.second.set_value(_save_camera(conn, cmd.first.cameras.front()));
                }
                else if(cmd.first.type == REMOVE_CAMERA)
                {
                    auto conn = _open_db(_top_dir);
                    if(cmd.first.cameras.empty())
                        R_THROW(("No cameras passed to REMOVE_CAMERA."));
                    cmd.second.set_value(_remove_camera(conn, cmd.first.cameras.front()));
                }
                else if(cmd.first.type == GET_MODIFIED_CAMERAS)
                {
                    auto conn = _open_db(_top_dir);
                    cmd.second.set_value(_get_modified_cameras(conn, cmd.first.cameras));
                }
                else if(cmd.first.type == GET_ASSIGNED_CAMERAS_ADDED)
                {
                    auto conn = _open_db(_top_dir);
                    cmd.second.set_value(_get_assigned_cameras_added(conn, cmd.first.cameras));
                }
                else if(cmd.first.type == GET_ASSIGNED_CAMERAS_REMOVED)
                {
                    auto conn = _open_db(_top_dir);
                    cmd.second.set_value(_get_assigned_cameras_removed(conn, cmd.first.cameras));
                }
                else if(cmd.first.type == GET_CREDENTIALS_BY_ID)
                {
                    auto conn = _open_db(_top_dir);
                    cmd.second.set_value(_get_credentials(conn, cmd.first.id));
                }
                else R_THROW(("Unknown work q command."));
            }
            catch(...)
            {
                // Nobody waits on these: without a log line a failed save is silent.
                if(cmd.first.type == INSERT_OR_UPDATE_DEVICES)
                    _log_failed_write(current_exception());
                try
                {
                    cmd.second.set_exception(current_exception());
                }
                catch(...)
                {
                    printf("EXCEPTION in error handling code for work q.");
                    fflush(stdout);
                    R_LOG_ERROR("EXCEPTION in error handling code for work q.");
                }
            }
        }

        // Update caches periodically (every 2 seconds when poll times out, or after processing commands)
        if(!db_created && chrono::steady_clock::now() >= next_create)
            create_db();
        _update_caches();
    }
}

void r_devices::_log_failed_write(exception_ptr e)
{
    // Only SQLite's own reason: the exception's message carries the whole query,
    // with the camera's address and credentials in it.
    string what = "the database refused the write";
    try
    {
        rethrow_exception(e);
    }
    catch(const std::exception& ex)
    {
        // r_sqlite_conn puts the reason after "Cause: " (a failed step) or
        // "failed with: " (a failed prepare).
        string msg = ex.what();
        for(string marker : {"Cause: ", "failed with: "})
        {
            auto at = msg.rfind(marker);
            if(at != string::npos && at + marker.size() < msg.size())
            {
                what = msg.substr(at + marker.size());
                break;
            }
        }
    }
    catch(...)
    {
    }

    // At most one line a minute, with a count of the failures in between, so a
    // failure that repeats on every discovery pass doesn't fill the log.
    auto now = chrono::steady_clock::now();
    if(_write_failures_logged && now - _last_write_failure_log < chrono::minutes(1))
    {
        ++_write_failures_unlogged;
        return;
    }
    R_LOG_ERROR("Saving discovered cameras failed: %s%s", what.c_str(),
                _write_failures_unlogged ? (" (and " + to_string(_write_failures_unlogged) + " more since the last report)").c_str() : "");
    _write_failures_logged = true;
    _write_failures_unlogged = 0;
    _last_write_failure_log = now;
}

void r_devices::_create_db(const std::string& top_dir) const
{
    auto db_dir = top_dir + PATH_SLASH + "db";
    if(!r_fs::file_exists(db_dir))
        r_fs::mkdir(db_dir);

    auto conn = r_sqlite_conn(db_dir + PATH_SLASH + "cameras.db");

    conn.exec(
        "CREATE TABLE IF NOT EXISTS cameras ("
            "id TEXT PRIMARY KEY NOT NULL UNIQUE, "
            "camera_name TEXT, "
            "friendly_name TEXT, "
            "ipv4 TEXT, "
            "xaddrs TEXT, "
            "address TEXT, "
            "rtsp_url TEXT, "
            "rtsp_username TEXT, "
            "rtsp_password TEXT, "
            "video_codec TEXT, "
            "video_codec_parameters TEXT, "
            "video_timebase INTEGER, "
            "audio_codec TEXT, "
            "audio_codec_parameters TEXT, "
            "audio_timebase INTEGER, "
            "state TEXT, "
            "record_file_path TEXT, "
            "n_record_file_blocks INTEGER, "
            "record_file_block_size INTEGER, "
            "do_motion_detection INTEGER, "
            "motion_detection_file_path TEXT, "

            "last_update_time INTEGER, "
            "stream_config_hash TEXT"
        ");"
    );

    auto found_version = _get_db_version(conn);
    _upgrade_db(conn);
    // Which file, and its schema: a gateway can have more than one data dir.
    R_LOG_INFO("Cameras database: %s (schema version %d, was %d)", (db_dir + PATH_SLASH + "cameras.db").c_str(), _get_db_version(conn), found_version);
}

r_sqlite_conn r_devices::_open_db(const std::string& top_dir, bool rw) const
{
    auto db_dir = top_dir + PATH_SLASH + "db";
    return r_sqlite_conn(db_dir + PATH_SLASH + "cameras.db", rw);
}

// The columns each schema version added to the cameras table, in order. To
// change the schema, add rows here with the next version.
namespace
{
struct camera_column
{
    int version;
    const char* name;
    const char* type;
};

const camera_column CAMERA_COLUMNS[] = {
    {2, "do_motion_pruning", "INTEGER DEFAULT 0"},
    {2, "min_continuous_recording_hours", "INTEGER DEFAULT 24"},
    {3, "onvif_profile_token", "TEXT"},
    {4, "onvif_port", "INTEGER"},
    {4, "onvif_protocol", "TEXT"},
    {5, "motion_tier", "TEXT"},
    {6, "motion_tier_verified", "INTEGER"},
    // Null on existing rows means TCP, the default.
    {7, "rtsp_transport", "TEXT"},
    // ONVIF GetDeviceInformation, shown in the UI. Null on existing rows until
    // the camera is next interrogated.
    {8, "manufacturer", "TEXT"},
    {8, "model", "TEXT"},
    {8, "firmware_version", "TEXT"},
    // The camera's MAC address, filled in when it's next seen at its address.
    {9, "mac", "TEXT"},
    // The substream (r_camera::sub_*). Existing cameras read as off, so an
    // upgrade opens no second stream.
    {10, "sub_mode", "TEXT DEFAULT 'off'"},
    {10, "sub_status", "TEXT"},
    {10, "sub_onvif_profile_token", "TEXT"},
    {10, "sub_rtsp_url", "TEXT"},
    {10, "sub_video_codec", "TEXT"},
    {10, "sub_video_codec_parameters", "TEXT"},
    {10, "sub_video_timebase", "INTEGER"},
    {10, "sub_width", "INTEGER"},
    {10, "sub_height", "INTEGER"},
    {10, "sub_key_interval_ms", "INTEGER"}
};

const int CAMERA_DB_VERSION = 10;
}

void r_devices::_upgrade_db(const r_sqlite_conn& conn) const
{
    // Every start adds whichever of the columns are missing, whatever the stored
    // version says. A database can reach a version by another path: two builds
    // once both used version 8 for different columns, so a database at 8 may have
    // either set. Adding only what's missing makes each step safe to repeat, and
    // the version only ever moves forward.
    auto current_version = _get_db_version(conn);

    r_sqlite_transaction(conn, true, [&](const r_sqlite_conn& conn){
        auto existing = _camera_columns(conn);
        for(auto& c : CAMERA_COLUMNS)
        {
            if(existing.count(c.name) == 0)
                conn.exec(string("ALTER TABLE cameras ADD COLUMN ") + c.name + " " + c.type + ";");
        }
        if(current_version < CAMERA_DB_VERSION)
            _set_db_version(conn, CAMERA_DB_VERSION);
    });
}

set<string> r_devices::_camera_columns(const r_db::r_sqlite_conn& conn) const
{
    set<string> names;
    for(auto& row : conn.exec("PRAGMA table_info(cameras);"))
    {
        auto it = row.find("name");
        if(it != row.end() && !it->second.is_null())
            names.insert(it->second.value());
    }
    return names;
}

int r_devices::_get_db_version(const r_db::r_sqlite_conn& conn) const
{
    auto result = conn.exec("PRAGMA user_version;");
    if(result.empty())
        R_THROW(("Unable to query database version."));

    auto row = result.front();
    if(row.empty())
        R_THROW(("Invalid result from database query while fetching db version."));

    return s_to_int(row.begin()->second.value());
}

void r_devices::_set_db_version(const r_db::r_sqlite_conn& conn, int version) const
{
    conn.exec("PRAGMA user_version=" + to_string(version) + ";");
}

string r_devices::_create_insert_or_update_query(const r_db::r_sqlite_conn& conn, const r_stream_config& stream_config, const std::string& hash) const
{
    // Why not replace into? Because replace into replaces the whole row, and for the update case here we need
    // to make sure we only update the columns we have values for.
    auto result = conn.exec("SELECT * FROM cameras WHERE id='" + stream_config.id + "';");

    string query;

    if(result.empty())
    {
        query = r_string_utils::format(
            "INSERT INTO cameras (id, %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%sstate, last_update_time, stream_config_hash) "
            "VALUES('%s', %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s'discovered', %s, '%s');",

            (!stream_config.camera_name.is_null())?"camera_name, ":"",
            (!stream_config.ipv4.is_null())?"ipv4, ":"",
            (!stream_config.xaddrs.is_null())?"xaddrs, ":"",
            (!stream_config.manufacturer.is_null())?"manufacturer, ":"",
            (!stream_config.model.is_null())?"model, ":"",
            (!stream_config.firmware_version.is_null())?"firmware_version, ":"",
            (!stream_config.port.is_null())?"onvif_port, ":"",
            (!stream_config.protocol.is_null())?"onvif_protocol, ":"",
            (!stream_config.address.is_null())?"address, ":"",
            (!stream_config.rtsp_url.is_null())?"rtsp_url, ":"",
            (!stream_config.video_codec.is_null())?"video_codec, ":"",
            (!stream_config.video_codec_parameters.is_null())?"video_codec_parameters, ":"",
            (!stream_config.video_timebase.is_null())?"video_timebase, ":"",
            (!stream_config.audio_codec.is_null())?"audio_codec, ":"",
            (!stream_config.audio_codec_parameters.is_null())?"audio_codec_parameters, ":"",
            (!stream_config.audio_timebase.is_null())?"audio_timebase, ":"",

            stream_config.id.c_str(),
            (!stream_config.camera_name.is_null())?r_string_utils::format("'%s', ", stream_config.camera_name.value().c_str()).c_str():"",
            (!stream_config.ipv4.is_null())?r_string_utils::format("'%s', ", stream_config.ipv4.value().c_str()).c_str():"",
            (!stream_config.xaddrs.is_null())?r_string_utils::format("'%s', ", stream_config.xaddrs.value().c_str()).c_str():"",
            (!stream_config.manufacturer.is_null())?r_string_utils::format("'%s', ", stream_config.manufacturer.value().c_str()).c_str():"",
            (!stream_config.model.is_null())?r_string_utils::format("'%s', ", stream_config.model.value().c_str()).c_str():"",
            (!stream_config.firmware_version.is_null())?r_string_utils::format("'%s', ", stream_config.firmware_version.value().c_str()).c_str():"",
            (!stream_config.port.is_null())?r_string_utils::format("%d, ", stream_config.port.value()).c_str():"",
            (!stream_config.protocol.is_null())?r_string_utils::format("'%s', ", stream_config.protocol.value().c_str()).c_str():"",
            (!stream_config.address.is_null())?r_string_utils::format("'%s', ", stream_config.address.value().c_str()).c_str():"",
            (!stream_config.rtsp_url.is_null())?r_string_utils::format("'%s', ", stream_config.rtsp_url.value().c_str()).c_str():"",
            (!stream_config.video_codec.is_null())?r_string_utils::format("'%s', ", stream_config.video_codec.value().c_str()).c_str():"",
            (!stream_config.video_codec_parameters.is_null())?r_string_utils::format("'%s', ", stream_config.video_codec_parameters.value().c_str()).c_str():"",
            (!stream_config.video_timebase.is_null())?r_string_utils::format("'%d', ", stream_config.video_timebase.value()).c_str():"",
            (!stream_config.audio_codec.is_null())?r_string_utils::format("'%s', ", stream_config.audio_codec.value().c_str()).c_str():"",
            (!stream_config.audio_codec_parameters.is_null())?r_string_utils::format("'%s', ", stream_config.audio_codec_parameters.value().c_str()).c_str():"",
            (!stream_config.audio_timebase.is_null())?r_string_utils::format("'%d', ", stream_config.audio_timebase.value()).c_str():"",
            "unixepoch()",
            hash.c_str()
        );
    }
    else
    {
        query = r_string_utils::format(
            "UPDATE cameras SET "
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "last_update_time=%s, "
                "stream_config_hash='%s' "
            "WHERE id='%s';",
            (!stream_config.camera_name.is_null())?r_string_utils::format("camera_name='%s', ", stream_config.camera_name.value().c_str()).c_str():"",
            (!stream_config.ipv4.is_null())?r_string_utils::format("ipv4='%s', ", stream_config.ipv4.value().c_str()).c_str():"",
            (!stream_config.xaddrs.is_null())?r_string_utils::format("xaddrs='%s', ", stream_config.xaddrs.value().c_str()).c_str():"",
            (!stream_config.manufacturer.is_null())?r_string_utils::format("manufacturer='%s', ", stream_config.manufacturer.value().c_str()).c_str():"",
            (!stream_config.model.is_null())?r_string_utils::format("model='%s', ", stream_config.model.value().c_str()).c_str():"",
            (!stream_config.firmware_version.is_null())?r_string_utils::format("firmware_version='%s', ", stream_config.firmware_version.value().c_str()).c_str():"",
            (!stream_config.port.is_null())?r_string_utils::format("onvif_port=%d, ", stream_config.port.value()).c_str():"",
            (!stream_config.protocol.is_null())?r_string_utils::format("onvif_protocol='%s', ", stream_config.protocol.value().c_str()).c_str():"",
            (!stream_config.address.is_null())?r_string_utils::format("address='%s', ", stream_config.address.value().c_str()).c_str():"",
            (!stream_config.rtsp_url.is_null())?r_string_utils::format("rtsp_url='%s', ", stream_config.rtsp_url.value().c_str()).c_str():"",
            (!stream_config.video_codec.is_null())?r_string_utils::format("video_codec='%s', ", stream_config.video_codec.value().c_str()).c_str():"",
            (!stream_config.video_codec_parameters.is_null())?r_string_utils::format("video_codec_parameters='%s', ", stream_config.video_codec_parameters.value().c_str()).c_str():"",
            (!stream_config.video_timebase.is_null())?r_string_utils::format("video_timebase='%d', ", stream_config.video_timebase.value()).c_str():"",
            (!stream_config.audio_codec.is_null())?r_string_utils::format("audio_codec='%s', ", stream_config.audio_codec.value().c_str()).c_str():"",
            (!stream_config.audio_codec_parameters.is_null())?r_string_utils::format("audio_codec_parameters='%s', ", stream_config.audio_codec_parameters.value().c_str()).c_str():"",
            (!stream_config.audio_timebase.is_null())?r_string_utils::format("audio_timebase='%d', ", stream_config.audio_timebase.value()).c_str():"",
            "unixepoch()",
            hash.c_str(),
            stream_config.id.c_str()
        );
    }

    return query;
}

r_camera r_devices::_create_camera(const map<string, r_nullable<string>>& row) const
{
    r_camera camera;
    camera.id = row.at("id").value();
    if(!row.at("camera_name").is_null())
        camera.camera_name = row.at("camera_name").value();
    if(!row.at("friendly_name").is_null())
        camera.friendly_name = row.at("friendly_name").value();
    if(!row.at("ipv4").is_null())
        camera.ipv4 = row.at("ipv4").value();
    if(!row.at("xaddrs").is_null())
        camera.xaddrs = row.at("xaddrs").value();
    if(row.count("manufacturer") && !row.at("manufacturer").is_null())
        camera.manufacturer = row.at("manufacturer").value();
    if(row.count("model") && !row.at("model").is_null())
        camera.model = row.at("model").value();
    if(row.count("firmware_version") && !row.at("firmware_version").is_null())
        camera.firmware_version = row.at("firmware_version").value();
    if(!row.at("onvif_port").is_null())
        camera.onvif_port = s_to_int(row.at("onvif_port").value());
    if(!row.at("onvif_protocol").is_null())
        camera.onvif_protocol = row.at("onvif_protocol").value();
    if(!row.at("address").is_null())
        camera.address = row.at("address").value();
    if(!row.at("rtsp_url").is_null())
        camera.rtsp_url = row.at("rtsp_url").value();
    if(!row.at("onvif_profile_token").is_null())
        camera.onvif_profile_token = row.at("onvif_profile_token").value();
    if(!row.at("rtsp_username").is_null())
    {
        // Decrypt username
        try {
            camera.rtsp_username = _decrypt_credential(row.at("rtsp_username").value());
        } catch (const r_exception& e) {
            R_LOG_WARNING("Failed to decrypt username for camera %s: %s", camera.id.c_str(), e.what());
            camera.rtsp_username = row.at("rtsp_username").value(); // Return as-is if decryption fails
        }
    }
    if(!row.at("rtsp_password").is_null())
    {
        // Decrypt password
        try {
            camera.rtsp_password = _decrypt_credential(row.at("rtsp_password").value());
        } catch (const r_exception& e) {
            R_LOG_WARNING("Failed to decrypt password for camera %s: %s", camera.id.c_str(), e.what());
            camera.rtsp_password = row.at("rtsp_password").value(); // Return as-is if decryption fails
        }
    }
    if(!row.at("video_codec").is_null())
        camera.video_codec = row.at("video_codec").value();
    if(!row.at("video_codec_parameters").is_null())
        camera.video_codec_parameters = row.at("video_codec_parameters").value();
    if(!row.at("video_timebase").is_null())    
        camera.video_timebase = s_to_int(row.at("video_timebase").value());
    if(!row.at("audio_codec").is_null())
        camera.audio_codec = row.at("audio_codec").value();
    if(!row.at("audio_codec_parameters").is_null())
        camera.audio_codec_parameters = row.at("audio_codec_parameters").value();
    if(!row.at("audio_timebase").is_null())
        camera.audio_timebase = s_to_int(row.at("audio_timebase").value());
    camera.state = row.at("state").value();
    if(!row.at("record_file_path").is_null())
        camera.record_file_path = row.at("record_file_path").value();
    if(!row.at("n_record_file_blocks").is_null())
        camera.n_record_file_blocks = s_to_int(row.at("n_record_file_blocks").value());
    if(!row.at("record_file_block_size").is_null())
        camera.record_file_block_size = s_to_int(row.at("record_file_block_size").value());
    if(!row.at("do_motion_detection").is_null())
        camera.do_motion_detection = (s_to_int(row.at("do_motion_detection").value()) == 1)?true:false;
    else camera.do_motion_detection = false;
    if(!row.at("motion_detection_file_path").is_null())
        camera.motion_detection_file_path = row.at("motion_detection_file_path").value();
    if(!row.at("do_motion_pruning").is_null())
        camera.do_motion_pruning = (s_to_int(row.at("do_motion_pruning").value()) == 1)?true:false;
    else camera.do_motion_pruning = false;
    if(!row.at("min_continuous_recording_hours").is_null())
        camera.min_continuous_recording_hours = (s_to_int(row.at("min_continuous_recording_hours").value()));
    if(row.count("motion_tier") && !row.at("motion_tier").is_null())
        camera.motion_tier = row.at("motion_tier").value();
    if(row.count("motion_tier_verified") && !row.at("motion_tier_verified").is_null())
        camera.motion_tier_verified = s_to_int(row.at("motion_tier_verified").value());
    if(row.count("rtsp_transport") && !row.at("rtsp_transport").is_null())
        camera.rtsp_transport = row.at("rtsp_transport").value();
    if(row.count("mac") && !row.at("mac").is_null())
        camera.mac = row.at("mac").value();

    auto text = [&](const char* col, r_nullable<string>& out){
        if(row.count(col) && !row.at(col).is_null())
            out = row.at(col).value();
    };
    auto number = [&](const char* col, r_nullable<int>& out){
        if(row.count(col) && !row.at(col).is_null())
            out = s_to_int(row.at(col).value());
    };
    text("sub_mode", camera.sub_mode);
    text("sub_status", camera.sub_status);
    text("sub_onvif_profile_token", camera.sub_onvif_profile_token);
    text("sub_rtsp_url", camera.sub_rtsp_url);
    text("sub_video_codec", camera.sub_video_codec);
    text("sub_video_codec_parameters", camera.sub_video_codec_parameters);
    number("sub_video_timebase", camera.sub_video_timebase);
    number("sub_width", camera.sub_width);
    number("sub_height", camera.sub_height);
    number("sub_key_interval_ms", camera.sub_key_interval_ms);

    camera.stream_config_hash = row.at("stream_config_hash").value();
    return camera;
}

r_devices_cmd_result r_devices::_insert_or_update_devices(const r_db::r_sqlite_conn& conn, const vector<pair<r_stream_config, string>>& stream_configs) const
{
    r_sqlite_transaction(conn, true, [&](const r_sqlite_conn& conn){
        for(auto& sc : stream_configs)
        {
            auto q = _create_insert_or_update_query(conn, sc.first, sc.second);
            conn.exec(q);
        }
    });

    return r_devices_cmd_result();
}

r_devices_cmd_result r_devices::_get_camera_by_id(const r_sqlite_conn& conn, const string& id) const
{
    r_devices_cmd_result result;
    r_sqlite_transaction(conn, false, [&](const r_sqlite_conn& conn){
        auto qr = conn.exec("SELECT * FROM cameras WHERE id='" + id + "';");
        if(!qr.empty())
            result.cameras.push_back(_create_camera(qr.front()));
    });
    return result;
}

r_devices_cmd_result r_devices::_get_all_cameras(const r_sqlite_conn& conn) const
{
    r_devices_cmd_result result;
    r_sqlite_transaction(conn, false, [&](const r_sqlite_conn& conn){
        auto cameras = conn.exec("SELECT * FROM cameras;");
        for(auto& c : cameras)
            result.cameras.push_back(_create_camera(c));
    });

    return result;
}

r_devices_cmd_result r_devices::_get_assigned_cameras(const r_sqlite_conn& conn) const
{
    r_devices_cmd_result result;

    r_sqlite_transaction(conn, false, [&](const r_sqlite_conn& conn){
        auto cameras = conn.exec("SELECT * FROM cameras WHERE state=\"assigned\";");
        for(auto& c : cameras)
            result.cameras.push_back(_create_camera(c));
    });

    return result;
}

r_devices_cmd_result r_devices::_save_camera(const r_sqlite_conn& conn, const r_camera& camera) const
{
    r_stream_config sc;
    sc.id = camera.id;
    sc.camera_name = camera.camera_name;
    sc.ipv4 = camera.ipv4;
    sc.xaddrs = camera.xaddrs;
    sc.address = camera.address;
    sc.rtsp_url = camera.rtsp_url;
    sc.video_codec = camera.video_codec;
    sc.video_codec_parameters = camera.video_codec_parameters;
    sc.video_timebase = camera.video_timebase;
    sc.audio_codec = camera.audio_codec;
    sc.audio_codec_parameters = camera.audio_codec_parameters;
    sc.audio_timebase = camera.audio_timebase;

    auto hash = hash_stream_config(sc);

    r_devices_cmd_result result;

    // Encrypt credentials before saving
    // Only "TCP" or "UDP" reach the SQL below (it's formatted, not bound):
    // anything else is stored as TCP, the default.
    r_nullable<string> rtsp_transport;
    if(!camera.rtsp_transport.is_null())
        rtsp_transport.set_value(camera.rtsp_transport.value() == "UDP" ? string("UDP") : string("TCP"));

    // Only a well-formed MAC reaches the SQL below (formatted, not bound).
    r_nullable<string> mac;
    if(!camera.mac.is_null())
        mac = r_utils::r_networking::r_normalize_mac(camera.mac.value());

    r_camera encrypted_camera = camera;
    if(!encrypted_camera.rtsp_username.is_null())
    {
        try {
            encrypted_camera.rtsp_username = _encrypt_credential(encrypted_camera.rtsp_username.value());
        } catch (const r_exception& e) {
            R_LOG_ERROR("Failed to encrypt username for camera %s: %s", camera.id.c_str(), e.what());
            throw;
        }
    }
    if(!encrypted_camera.rtsp_password.is_null())
    {
        try {
            encrypted_camera.rtsp_password = _encrypt_credential(encrypted_camera.rtsp_password.value());
        } catch (const r_exception& e) {
            R_LOG_ERROR("Failed to encrypt password for camera %s: %s", camera.id.c_str(), e.what());
            throw;
        }
    }

    auto query = r_string_utils::format(
            "REPLACE INTO cameras("
                "id, %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%sstate, %s%s%s%s%s%s%s%s%s%s%slast_update_time, stream_config_hash) "
            "VALUES("
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
                "%s"
            ");",
            (!camera.camera_name.is_null())?"camera_name, ":"",
            (!camera.friendly_name.is_null())?"friendly_name, ":"",
            (!camera.ipv4.is_null())?"ipv4, ":"",
            (!camera.xaddrs.is_null())?"xaddrs, ":"",
            (!camera.manufacturer.is_null())?"manufacturer, ":"",
            (!camera.model.is_null())?"model, ":"",
            (!camera.firmware_version.is_null())?"firmware_version, ":"",
            (!camera.onvif_port.is_null())?"onvif_port, ":"",
            (!camera.onvif_protocol.is_null())?"onvif_protocol, ":"",
            (!camera.address.is_null())?"address, ":"",
            (!camera.rtsp_url.is_null())?"rtsp_url, ":"",
            (!camera.onvif_profile_token.is_null())?"onvif_profile_token, ":"",
            (!camera.rtsp_username.is_null())?"rtsp_username, ":"",
            (!camera.rtsp_password.is_null())?"rtsp_password, ":"",
            (!camera.video_codec.is_null())?"video_codec, ":"",
            (!camera.video_codec_parameters.is_null())?"video_codec_parameters, ":"",
            (!camera.video_timebase.is_null())?"video_timebase, ":"",
            (!camera.audio_codec.is_null())?"audio_codec, ":"",
            (!camera.audio_codec_parameters.is_null())?"audio_codec_parameters, ":"",
            (!camera.audio_timebase.is_null())?"audio_timebase, ":"",
            (!camera.record_file_path.is_null())?"record_file_path, ":"",
            (!camera.n_record_file_blocks.is_null())?"n_record_file_blocks, ":"",
            (!camera.record_file_block_size.is_null())?"record_file_block_size, ":"",
            (!camera.do_motion_detection.is_null())?"do_motion_detection, ":"",
            (!camera.motion_detection_file_path.is_null())?"motion_detection_file_path, ":"",
            (!camera.do_motion_pruning.is_null())?"do_motion_pruning, ":"",
            (!camera.min_continuous_recording_hours.is_null())?"min_continuous_recording_hours, ":"",
            (!camera.motion_tier.is_null())?"motion_tier, ":"",
            (!camera.motion_tier_verified.is_null())?"motion_tier_verified, ":"",
            (!rtsp_transport.is_null())?"rtsp_transport, ":"",
            (!mac.is_null())?"mac, ":"",

            r_string_utils::format("'%s', ", camera.id.c_str()).c_str(),
            (!camera.camera_name.is_null())?r_string_utils::format("'%s', ", camera.camera_name.value().c_str()).c_str():"",
            (!camera.friendly_name.is_null())?r_string_utils::format("'%s', ", camera.friendly_name.value().c_str()).c_str():"",
            (!camera.ipv4.is_null())?r_string_utils::format("'%s', ", camera.ipv4.value().c_str()).c_str():"",
            (!camera.xaddrs.is_null())?r_string_utils::format("'%s', ", camera.xaddrs.value().c_str()).c_str():"",
            (!camera.manufacturer.is_null())?r_string_utils::format("'%s', ", camera.manufacturer.value().c_str()).c_str():"",
            (!camera.model.is_null())?r_string_utils::format("'%s', ", camera.model.value().c_str()).c_str():"",
            (!camera.firmware_version.is_null())?r_string_utils::format("'%s', ", camera.firmware_version.value().c_str()).c_str():"",
            (!camera.onvif_port.is_null())?r_string_utils::format("%d, ", camera.onvif_port.value()).c_str():"",
            (!camera.onvif_protocol.is_null())?r_string_utils::format("'%s', ", camera.onvif_protocol.value().c_str()).c_str():"",
            (!camera.address.is_null())?r_string_utils::format("'%s', ", camera.address.value().c_str()).c_str():"",
            (!camera.rtsp_url.is_null())?r_string_utils::format("'%s', ", camera.rtsp_url.value().c_str()).c_str():"",
            (!camera.onvif_profile_token.is_null())?r_string_utils::format("'%s', ", camera.onvif_profile_token.value().c_str()).c_str():"",
            (!encrypted_camera.rtsp_username.is_null())?r_string_utils::format("'%s', ", encrypted_camera.rtsp_username.value().c_str()).c_str():"",
            (!encrypted_camera.rtsp_password.is_null())?r_string_utils::format("'%s', ", encrypted_camera.rtsp_password.value().c_str()).c_str():"",
            (!camera.video_codec.is_null())?r_string_utils::format("'%s', ", camera.video_codec.value().c_str()).c_str():"",
            (!camera.video_codec_parameters.is_null())?r_string_utils::format("'%s', ", camera.video_codec_parameters.value().c_str()).c_str():"",
            (!camera.video_timebase.is_null())?r_string_utils::format("%d, ", camera.video_timebase.value()).c_str():"",
            (!camera.audio_codec.is_null())?r_string_utils::format("'%s', ", camera.audio_codec.value().c_str()).c_str():"",
            (!camera.audio_codec_parameters.is_null())?r_string_utils::format("'%s', ", camera.audio_codec_parameters.value().c_str()).c_str():"",
            (!camera.audio_timebase.is_null())?r_string_utils::format("%d, ", camera.audio_timebase.value()).c_str():"",
            r_string_utils::format("'%s', ", camera.state.c_str()).c_str(),
            (!camera.record_file_path.is_null())?r_string_utils::format("'%s', ", camera.record_file_path.value().c_str()).c_str():"",
            (!camera.n_record_file_blocks.is_null())?r_string_utils::format("%d, ", camera.n_record_file_blocks.value()).c_str():"",
            (!camera.record_file_block_size.is_null())?r_string_utils::format("%d, ", camera.record_file_block_size.value()).c_str():"",
            (!camera.do_motion_detection.is_null())?r_string_utils::format("%d, ", (camera.do_motion_detection.value()==true)?1:0).c_str():"",
            (!camera.motion_detection_file_path.is_null())?r_string_utils::format("'%s', ", camera.motion_detection_file_path.value().c_str()).c_str():"",
            (!camera.do_motion_pruning.is_null())?r_string_utils::format("%d, ", (camera.do_motion_pruning.value()==true)?1:0).c_str():"",
            (!camera.min_continuous_recording_hours.is_null())?r_string_utils::format("%d, ", camera.min_continuous_recording_hours.value()).c_str():"",
            (!camera.motion_tier.is_null())?r_string_utils::format("'%s', ", camera.motion_tier.value().c_str()).c_str():"",
            (!camera.motion_tier_verified.is_null())?r_string_utils::format("%d, ", camera.motion_tier_verified.value()).c_str():"",
            (!rtsp_transport.is_null())?r_string_utils::format("'%s', ", rtsp_transport.value().c_str()).c_str():"",
            (!mac.is_null())?r_string_utils::format("'%s', ", mac.value().c_str()).c_str():"",

            "unixepoch(), ",
            r_string_utils::format("'%s'", hash.c_str()).c_str()
        );

    r_sqlite_transaction(conn, true, [&](const r_sqlite_conn& conn){
        conn.exec(query);
        _save_substream(conn, camera);
    });

    return result;
}

// The substream fields, bound rather than formatted into the SQL: the URL and
// codec parameters come from the camera. Every field is written, so a null
// clears it; sub_mode is only ever "on" or "off".
void r_devices::_save_substream(const r_sqlite_conn& conn, const r_camera& camera) const
{
    auto stmt = conn.prepare(
        "UPDATE cameras SET sub_mode = ?, sub_status = ?, sub_onvif_profile_token = ?, sub_rtsp_url = ?, "
        "sub_video_codec = ?, sub_video_codec_parameters = ?, sub_video_timebase = ?, sub_width = ?, sub_height = ?, "
        "sub_key_interval_ms = ? WHERE id = ?;"
    );
    int i = 1;
    auto text = [&](const r_nullable<string>& v){
        if(v.is_null()) stmt.bind_null(i++);
        else stmt.bind(i++, v.value());
    };
    auto number = [&](const r_nullable<int>& v){
        if(v.is_null()) stmt.bind_null(i++);
        else stmt.bind(i++, v.value());
    };
    stmt.bind(i++, string(substream_on(camera) ? "on" : "off"));
    text(camera.sub_status);
    text(camera.sub_onvif_profile_token);
    text(camera.sub_rtsp_url);
    text(camera.sub_video_codec);
    text(camera.sub_video_codec_parameters);
    number(camera.sub_video_timebase);
    number(camera.sub_width);
    number(camera.sub_height);
    number(camera.sub_key_interval_ms);
    stmt.bind(i++, camera.id);
    stmt.exec_no_result();
}

r_devices_cmd_result r_devices::_remove_camera(const r_sqlite_conn& conn, const r_camera& camera) const
{
    r_sqlite_transaction(conn, true, [&](const r_sqlite_conn& conn){
        conn.exec("DELETE FROM cameras WHERE id='" + camera.id + "';");
    });

    return r_devices_cmd_result();
}

r_devices_cmd_result r_devices::_get_modified_cameras(const r_sqlite_conn& conn, const vector<r_camera>& cameras) const
{
    vector<r_camera> out_cameras;
    r_sqlite_transaction(conn, false, [&](const r_sqlite_conn& conn){
        for(auto& c : cameras)
        {
            auto query = r_string_utils::format(
                "SELECT * FROM cameras WHERE id=\"%s\" AND stream_config_hash != \"%s\";",
                c.id.c_str(),
                c.stream_config_hash.c_str()
            );

            auto modified = conn.exec(query);
            if(!modified.empty())
                out_cameras.push_back(_create_camera(modified.front()));
        }
    });

    r_devices_cmd_result result;
    result.cameras = out_cameras;

    return result;
}

r_devices_cmd_result r_devices::_get_assigned_cameras_added(const r_sqlite_conn& conn, const vector<r_camera>& cameras) const
{
    vector<string> input_ids;
    transform(begin(cameras), end(cameras), back_inserter(input_ids),[](const r_camera& c){return c.id;});

    auto qr = conn.exec("SELECT id FROM cameras WHERE state='assigned';");

    vector<string> db_ids;
    transform(begin(qr), end(qr), back_inserter(db_ids), [](const map<string, r_nullable<string>>& r){return r.at("id").value();});

    // set_diff(a, b) - Returns the items in a that are not in b
    auto added_ids = set_diff(db_ids, input_ids);

    r_devices_cmd_result result;

    for(auto added_id : added_ids)
    {
        qr = conn.exec("SELECT * FROM cameras WHERE id='" + added_id + "';");
        if(!qr.empty())
            result.cameras.push_back(_create_camera(qr.front()));
    }

    return result;
}

r_devices_cmd_result r_devices::_get_assigned_cameras_removed(const r_sqlite_conn& conn, const vector<r_camera>& cameras) const
{
    map<string, r_camera> cmap;
    for(auto& c : cameras)
        cmap[c.id] = c;

    vector<string> input_ids;
    transform(begin(cameras), end(cameras), back_inserter(input_ids),[](const r_camera& c){return c.id;});

    auto qr = conn.exec("SELECT id FROM cameras WHERE state='assigned';");

    vector<string> db_ids;
    transform(begin(qr), end(qr), back_inserter(db_ids), [](const map<string, r_nullable<string>>& r){return r.at("id").value();});

    // set_diff(a, b) - Returns the items in a that are not in b
    auto removed_ids = set_diff(input_ids, db_ids);

    r_devices_cmd_result result;
    transform(begin(removed_ids), end(removed_ids), back_inserter(result.cameras),[&cmap](const string& id){return cmap[id];});

    return result;
}

r_devices_cmd_result r_devices::_get_credentials(const r_sqlite_conn& conn, const string& id)
{
    r_devices_cmd_result result;
    r_sqlite_transaction(conn, false, [&](const r_sqlite_conn& conn){
        auto qr = conn.exec("SELECT rtsp_username, rtsp_password FROM cameras WHERE id='" + id + "';");
        if(!qr.empty())
        {
            auto row = qr.front();

            auto found = row.find("rtsp_username");
            if(found != row.end() && !found->second.is_null())
            {
                // Decrypt username
                try {
                    result.credentials.first = _decrypt_credential(found->second.value());
                } catch (const r_exception& e) {
                    R_LOG_WARNING("Failed to decrypt username for camera %s: %s", id.c_str(), e.what());
                    result.credentials.first = found->second.value(); // Return as-is if decryption fails
                }
            }

            found = row.find("rtsp_password");
            if(found != row.end() && !found->second.is_null())
            {
                // Decrypt password
                try {
                    result.credentials.second = _decrypt_credential(found->second.value());
                } catch (const r_exception& e) {
                    R_LOG_WARNING("Failed to decrypt password for camera %s: %s", id.c_str(), e.what());
                    result.credentials.second = found->second.value(); // Return as-is if decryption fails
                }
            }
        }
    });
    return result;
}

vector<uint8_t> r_devices::_get_master_key() const
{
    if(!_master_key_loaded)
    {
        r_secure_store store;
        _master_key = store.get_master_key();
        _master_key_loaded = true;
    }
    return _master_key;
}

string r_devices::_encrypt_credential(const string& plaintext) const
{
    if(plaintext.empty())
        return plaintext;

    try {
        auto master_key = _get_master_key();
        return r_credential_crypto::encrypt_credential(plaintext, master_key);
    } catch (const r_exception& e) {
        R_LOG_ERROR("Failed to encrypt credential: %s", e.what());
        throw;
    }
}

string r_devices::_decrypt_credential(const string& encrypted) const
{
    if(encrypted.empty())
        return encrypted;

    try {
        auto master_key = _get_master_key();
        return r_credential_crypto::decrypt_credential(encrypted, master_key);
    } catch (const r_exception& e) {
        R_LOG_ERROR("Failed to decrypt credential: %s", e.what());
        throw;
    }
}
