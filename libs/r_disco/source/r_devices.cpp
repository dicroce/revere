
#include "r_disco/r_devices.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_functional.h"
#include "r_utils/r_secure_store.h"
#include "r_utils/r_credential_crypto.h"
#include <thread>
#include <algorithm>
#include <iterator>
#include <chrono>

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

void r_devices::_entry_point()
{
    _create_db(_top_dir);

    while(_running)
    {
        auto maybe_cmd = _db_work_q.poll();
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
    }
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

    _upgrade_db(conn);
}

r_sqlite_conn r_devices::_open_db(const std::string& top_dir, bool rw) const
{
    auto db_dir = top_dir + PATH_SLASH + "db";
    return r_sqlite_conn(db_dir + PATH_SLASH + "cameras.db", rw);
}

void r_devices::_upgrade_db(const r_sqlite_conn& conn) const
{
    auto current_version = _get_db_version(conn);

    switch(current_version)
    {
        // case 0 can serve as an example and model for future upgrades
        // Note: the cases purposefully fall through.
        case 0:
        {
            r_sqlite_transaction(conn, true, [&](const r_sqlite_conn& conn){
                // perform alter statements here
                _set_db_version(conn, 1);
            });
        }
        [[fallthrough]];
        case 1:
        {
            r_sqlite_transaction(conn, true, [&](const r_sqlite_conn& conn){
                conn.exec(
                    "ALTER TABLE cameras ADD COLUMN do_motion_pruning INTEGER DEFAULT 0;"
                );
                conn.exec(
                    "ALTER TABLE cameras ADD COLUMN min_continuous_recording_hours INTEGER DEFAULT 24;"
                );
                _set_db_version(conn, 2);
            });
        }
        [[fallthrough]];
        default:
            break;
    };
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
            "INSERT INTO cameras (id, %s%s%s%s%s%s%s%s%s%s%sstate, last_update_time, stream_config_hash) "
            "VALUES('%s', %s%s%s%s%s%s%s%s%s%s%s'discovered', %s, '%s');",

            (!stream_config.camera_name.is_null())?"camera_name, ":"",
            (!stream_config.ipv4.is_null())?"ipv4, ":"",
            (!stream_config.xaddrs.is_null())?"xaddrs, ":"",
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
                "last_update_time=%s, "
                "stream_config_hash='%s' "
            "WHERE id='%s';",
            (!stream_config.camera_name.is_null())?r_string_utils::format("camera_name='%s', ", stream_config.camera_name.value().c_str()).c_str():"",
            (!stream_config.ipv4.is_null())?r_string_utils::format("ipv4='%s', ", stream_config.ipv4.value().c_str()).c_str():"",
            (!stream_config.xaddrs.is_null())?r_string_utils::format("xaddrs='%s', ", stream_config.xaddrs.value().c_str()).c_str():"",
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
    if(!row.at("address").is_null())
        camera.address = row.at("address").value();
    if(!row.at("rtsp_url").is_null())
        camera.rtsp_url = row.at("rtsp_url").value();
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
                "id, %s%s%s%s%s%s%s%s%s%s%s%s%s%sstate, %s%s%s%s%s%s%slast_update_time, stream_config_hash) "
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
            ");",
            (!camera.camera_name.is_null())?"camera_name, ":"",
            (!camera.friendly_name.is_null())?"friendly_name, ":"",
            (!camera.ipv4.is_null())?"ipv4, ":"",
            (!camera.xaddrs.is_null())?"xaddrs, ":"",
            (!camera.address.is_null())?"address, ":"",
            (!camera.rtsp_url.is_null())?"rtsp_url, ":"",
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

            r_string_utils::format("'%s', ", camera.id.c_str()).c_str(),
            (!camera.camera_name.is_null())?r_string_utils::format("'%s', ", camera.camera_name.value().c_str()).c_str():"",
            (!camera.friendly_name.is_null())?r_string_utils::format("'%s', ", camera.friendly_name.value().c_str()).c_str():"",
            (!camera.ipv4.is_null())?r_string_utils::format("'%s', ", camera.ipv4.value().c_str()).c_str():"",
            (!camera.xaddrs.is_null())?r_string_utils::format("'%s', ", camera.xaddrs.value().c_str()).c_str():"",
            (!camera.address.is_null())?r_string_utils::format("'%s', ", camera.address.value().c_str()).c_str():"",
            (!camera.rtsp_url.is_null())?r_string_utils::format("'%s', ", camera.rtsp_url.value().c_str()).c_str():"",
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

            "unixepoch(), ",
            r_string_utils::format("'%s'", hash.c_str()).c_str()
        );

    r_sqlite_transaction(conn, true, [&](const r_sqlite_conn& conn){
        conn.exec(query);
    });

    return result;
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
