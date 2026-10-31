
#include "r_vss/r_ws.h"
#include "r_vss/r_stream_keeper.h"
#include "r_vss/r_rtsp_auth.h"
#include "r_vss/r_query.h"
#include "r_vss/r_vss_utils.h"
#include "r_vss/r_camera_source_info.h"
#include "r_vss/r_motion_engine.h"
#include "r_vss/r_build_info.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_file.h"
#include "r_utils/r_ssl_server_socket.h"
#include "r_utils/r_local_api_token.h"
#include "r_utils/r_blob_tree.h"
#include "r_utils/r_qr_code.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_disco/r_camera.h"
#include "r_storage/r_storage_file.h"
#include "r_storage/r_storage_file_reader.h"
#include "r_storage/r_ring.h"
#include "r_pipeline/r_stream_info.h"
#include "r_av/r_muxer.h"
#include "r_av/r_video_decoder.h"
#include "r_av/r_video_encoder.h"
#include "r_av/r_audio_decoder.h"
#include "r_av/r_audio_encoder.h"
#include <functional>
#include <array>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>

using namespace r_utils;
using namespace r_http;
using namespace r_disco;
using namespace r_storage;
using namespace r_av;
using namespace r_vss;
using namespace std;
using namespace std::chrono;
using namespace std::placeholders;
using json = nlohmann::json;

const int WEB_SERVER_PORT = 8088;
const int WEB_SERVER_TLS_PORT = 8443;

namespace {

r_server_response mcp_ok(const json& id, const json& result)
{
    r_server_response resp;
    resp.set_content_type("application/json");
    resp.set_body(json({{"jsonrpc","2.0"},{"id",id},{"result",result}}).dump());
    return resp;
}

r_server_response mcp_err(const json& id, int code, const string& msg)
{
    r_server_response resp;
    resp.set_content_type("application/json");
    resp.set_body(json({{"jsonrpc","2.0"},{"id",id},{"error",{{"code",code},{"message",msg}}}}).dump());
    return resp;
}

// Reject export file names that could escape the exports directory or be used to
// inject into the Content-Disposition header on download. Accept a bare file
// name only: non-empty, bounded, no path separators, no drive/ADS colon, no
// NUL/control chars, and not a bare "." / "..".
bool is_safe_export_filename(const std::string& name)
{
    if(name.empty() || name.size() > 255)
        return false;
    if(name == "." || name == "..")
        return false;
    for(unsigned char c : name)
    {
        if(c == '/' || c == '\\' || c == ':' || c < 0x20 || c == 0x7f)
            return false;
    }
    return true;
}

// Cap on exports that are queued or running at once. Each export streams and
// (optionally) re-encodes footage to a file on disk, so an unbounded number of
// enqueued jobs is a CPU/disk exhaustion vector.
constexpr size_t MAX_INFLIGHT_EXPORTS = 8;

// The camera's resolution and framerate from its last recorded key frame
// (r_vss/r_camera_source_info.h). Empty for a camera that has no recording.
camera_source_info _camera_source_info(const std::string& top_dir, const r_disco::r_camera& c)
{
    if(c.record_file_path.is_null())
        return camera_source_info();

    // Path resolution mirrors _get_storage_path() in r_query.cpp.
    const auto& rfp = c.record_file_path.value();
    std::string path = (rfp.find('/') != std::string::npos || rfp.find('\\') != std::string::npos)
        ? rfp
        : (top_dir + PATH_SLASH + "video" + PATH_SLASH + rfp);
    return read_camera_source_info(path);
}

} // namespace

template<class SOK_T>
void r_ws::_add_routes(r_http::r_web_server<SOK_T>& server)
{
    // Handlers are transport-agnostic (they take r_socket_base&); this adapts
    // them to whichever socket type the server is instantiated with, so the
    // same table serves plain HTTP and HTTPS.
    //
    // Every route requires a valid token (r_ws_auth.h) except the handful
    // registered with open_route: what a client needs before it has one.
    // (The web UI's static files are served by the server's bundle route,
    // which is open too.)
    using handler = r_http::r_server_response (r_ws::*)(r_utils::r_socket_base&, const r_http::r_server_request&);
    auto add = [this, &server](int method, const char* path, handler fn, bool requires_auth)
    {
        server.add_route(method, path,
            [this, fn, requires_auth](const r_http::r_web_server<SOK_T>&, SOK_T& conn, const r_http::r_server_request& request)
            {
                if(requires_auth)
                    _require_auth(request);
                return (this->*fn)(conn, request);
            });
    };
    auto route = [&add](int method, const char* path, handler fn) { add(method, path, fn, true); };
    auto open_route = [&add](int method, const char* path, handler fn) { add(method, path, fn, false); };

    open_route(METHOD_GET, "/version", &r_ws::_get_version);
    route(METHOD_GET, "/cameras", &r_ws::_get_cameras);
    route(METHOD_GET, "/contents", &r_ws::_get_contents);
    route(METHOD_GET, "/motion_events", &r_ws::_get_motion_events);
    route(METHOD_GET, "/camera_events", &r_ws::_get_camera_events);
    route(METHOD_GET, "/analytics", &r_ws::_get_analytics);

    route(METHOD_GET, "/key_frame", &r_ws::_get_key_frame);
    route(METHOD_GET, "/video", &r_ws::_get_video);
    route(METHOD_GET, "/jpg", &r_ws::_get_jpg);
    route(METHOD_GET, "/webp", &r_ws::_get_webp);

    route(METHOD_GET, "/create_transcode_stream", &r_ws::_get_create_transcode_stream);
    route(METHOD_GET, "/transcode", &r_ws::_get_transcode);
    route(METHOD_GET, "/transcode_fmp4", &r_ws::_get_transcode_fmp4);
    route(METHOD_GET, "/finalize_transcode_stream", &r_ws::_get_finalize_transcode_stream);

    route(METHOD_GET, "/export", &r_ws::_get_export);
    route(METHOD_GET, "/transcode_export", &r_ws::_get_transcode_export);
    route(METHOD_GET, "/export_progress", &r_ws::_get_export_progress);
    route(METHOD_GET, "/export_download", &r_ws::_get_export_download);

    route(METHOD_POST, "/mcp", &r_ws::_post_mcp);

    // "Record" web flow: discover profiles, measure bitrate + snapshot, then
    // configure the camera for recording.
    route(METHOD_GET, "/camera_profiles", &r_ws::_get_camera_profiles);
    route(METHOD_POST, "/ptz", &r_ws::_post_ptz);
    route(METHOD_GET, "/ptz_view/frame", &r_ws::_get_ptz_view_frame);
    route(METHOD_GET, "/measure_camera", &r_ws::_get_measure_camera);
    route(METHOD_GET, "/measure_progress", &r_ws::_get_measure_progress);
    route(METHOD_GET, "/measure_result", &r_ws::_get_measure_result);
    route(METHOD_POST, "/configure_camera", &r_ws::_post_configure_camera);
    route(METHOD_POST, "/remove_camera", &r_ws::_post_remove_camera);
    route(METHOD_POST, "/update_camera_properties", &r_ws::_post_update_camera_properties);
    route(METHOD_POST, "/forget_camera", &r_ws::_post_forget_camera);
    route(METHOD_POST, "/add_rtsp_camera", &r_ws::_post_add_rtsp_camera);
    route(METHOD_GET, "/storage_info", &r_ws::_get_storage_info);
    route(METHOD_POST, "/storage_move", &r_ws::_post_storage_move);
    route(METHOD_POST, "/storage_change_retention", &r_ws::_post_storage_change_retention);
    route(METHOD_GET, "/storage_op", &r_ws::_get_storage_op);
    route(METHOD_POST, "/storage_op_cancel", &r_ws::_post_storage_op_cancel);
    route(METHOD_GET, "/system_plugins", &r_ws::_get_system_plugins);
    route(METHOD_POST, "/system_plugin_enabled", &r_ws::_post_system_plugin_enabled);
    route(METHOD_GET, "/keep_awake", &r_ws::_get_keep_awake);
    route(METHOD_POST, "/keep_awake", &r_ws::_post_keep_awake);

    open_route(METHOD_GET, "/auth_status", &r_ws::_get_auth_status);
    open_route(METHOD_POST, "/login", &r_ws::_post_login);
    open_route(METHOD_POST, "/logout", &r_ws::_post_logout);
    // Authenticates itself: the first password may be set without a token
    // (from this machine only), a change needs a token or the current one.
    open_route(METHOD_POST, "/set_password", &r_ws::_post_set_password);
    route(METHOD_POST, "/session", &r_ws::_post_session);
    route(METHOD_GET, "/rtsp_credentials", &r_ws::_get_rtsp_credentials);
}

r_ws::r_ws(const string& top_dir, r_devices& devices, r_agent& agent, r_stream_keeper& stream_keeper) :
    _top_dir(top_dir),
    _devices(devices),
    _agent(agent),
    _stream_keeper(stream_keeper),
    // Plain HTTP is always loopback only: this machine's web UI, vision, the
    // MCP bridge and plugins. HTTPS is loopback only too unless the user has
    // turned on remote access, in which case it listens on every interface so
    // vision and browsers elsewhere on the network can connect (every endpoint
    // requires a token; see r_ws_auth.h).
    _http(WEB_SERVER_PORT, "127.0.0.1"),
    _https(WEB_SERVER_TLS_PORT, _read_remote_access(top_dir) ? string() : string("127.0.0.1"))
{
    _remote_access_active = _read_remote_access(top_dir);
    _add_routes(_http);
    _add_routes(_https);

    // Cache the OS-protected master key once (used to encrypt/decrypt the system
    // password). Best-effort: if secure storage is unavailable the auth helpers
    // will fail closed (no token can be issued).
    try { _master_key = r_secure_store().get_master_key(); }
    catch(const exception& ex) { R_LOG_WARNING("r_ws: secure store unavailable, auth disabled: %s", ex.what()); }

    // TLS identity for the HTTPS listener: a self-signed certificate persisted
    // under <top_dir>/tls so its fingerprint stays stable for clients that pin
    // it. If this fails only the plain-HTTP listener comes up.
    try
    {
        _load_or_create_tls();
        _https.get_socket().set_config(_tls);
    }
    catch(const exception& ex)
    {
        R_LOG_ERROR("r_ws: TLS setup failed, HTTPS listener disabled: %s", ex.what());
        _tls.reset();
    }

    // Before the servers start (and before system plugins, which the stream
    // keeper starts after constructing us): local clients find it at once.
    try
    {
        _publish_local_token();
    }
    catch(const exception& ex)
    {
        R_LOG_ERROR("r_ws: could not publish the local API token; local clients (vision, MCP, plugins) will be refused: %s", ex.what());
    }

    // Load the web UI bundle if present alongside the binary (CWD is set to
    // the binary directory by _set_working_dir() before this is constructed).
    if(r_fs::file_exists("ui.rbt"))
    {
        try
        {
            ifstream f("ui.rbt", ios::binary);
            vector<uint8_t> buf((istreambuf_iterator<char>(f)), {});
            uint32_t version = 0;
            auto bundle = r_blob_tree::deserialize(buf.data(), buf.size(), version);
            _http.serve_bundle("/", bundle);
            _https.serve_bundle("/", std::move(bundle));
            R_LOG_INFO("Web UI available at http://localhost:%d/ and https://localhost:%d/", WEB_SERVER_PORT, WEB_SERVER_TLS_PORT);
        }
        catch(const exception& ex)
        {
            R_LOG_WARNING("Failed to load web UI bundle (ui.rbt): %s", ex.what());
        }
    }

    _http.start();
    if(_tls)
    {
        _https.start();
        R_LOG_INFO("r_ws: TLS certificate fingerprint (SHA-256) %s", _tls->fingerprint_sha256().c_str());
        if(_remote_access_active)
            R_LOG_INFO("r_ws: remote access ON: HTTPS listening on all interfaces, port %d", WEB_SERVER_TLS_PORT);
    }

    _export_running = true;
    _export_th = thread(&r_ws::_export_entry_point, this);

    _measure_running = true;
    _measure_th = thread(&r_ws::_measure_entry_point, this);
}

r_ws::~r_ws()
{
    stop();
}

void r_ws::stop()
{
    if(_export_running.exchange(false))
    {
        _export_q.wake();
        _export_th.join();
    }
    if(_measure_running.exchange(false))
    {
        _measure_q.wake();
        _measure_th.join();
    }
    // A storage operation runs to its end (only a move's copy can stop early);
    // its suspend/resume are served by the stream keeper's thread, which is
    // still running here.
    _storage_op.cancel_requested = true;
    if(_storage_th.joinable())
        _storage_th.join();
    _http.stop();
    _https.stop();

    // Withdraw the local token, but only if the file is still ours.
    auto ours = _auth.local_token();
    auto published = r_local_api_token::read(_top_dir);
    if(!ours.empty() && !published.is_null() && published.value() == ours)
        r_local_api_token::remove(_top_dir);
}

bool r_ws::_stopping() const
{
    return _http.stopping() || _https.stopping();
}

void r_ws::_publish_local_token()
{
    auto token = r_ws_auth::generate_token();
    _auth.set_local_token(token);
    r_local_api_token::write(_top_dir, token);
    R_LOG_INFO("r_ws: published local API token to %s", r_local_api_token::path(_top_dir).c_str());
}

r_nullable<string> r_ws::_request_token(const r_server_request& request)
{
    auto method = request.get_method();
    bool safe = (method == METHOD_GET || method == METHOD_HEAD);
    return r_ws_auth::extract_token(request.get_header("authorization"), request.get_header("cookie"), safe);
}

string r_ws::_peer_ip(r_socket_base& conn)
{
    try
    {
        return conn.get_peer_ip();
    }
    catch(...)
    {
        return string();
    }
}

bool r_ws::_is_tls(r_socket_base& conn)
{
    return dynamic_cast<r_ssl_server_socket*>(&conn) != nullptr;
}

void r_ws::_load_or_create_tls()
{
    auto dir = _top_dir + PATH_SLASH + "tls";
    auto crt_path = dir + PATH_SLASH + "server.crt";
    auto key_path = dir + PATH_SLASH + "server.key";

    if(r_fs::file_exists(crt_path) && r_fs::file_exists(key_path))
    {
        try
        {
            auto crt = r_fs::read_file(crt_path);
            auto key = r_fs::read_file(key_path);
            _tls = make_shared<r_ssl_server_config>(string(crt.begin(), crt.end()), string(key.begin(), key.end()));
            return;
        }
        catch(const exception& ex)
        {
            // Regenerate rather than refuse to start; clients that pinned the
            // old fingerprint will be asked to trust the new one.
            R_LOG_ERROR("r_ws: stored TLS certificate unusable (%s); generating a new one.", ex.what());
        }
    }

    auto pems = r_ssl_server_config::generate_self_signed("revere");
    r_fs::mkdir_p(dir);
    r_fs::atomic_write_file((const uint8_t*)pems.first.data(), pems.first.size(), crt_path);
    r_fs::atomic_write_private_file((const uint8_t*)pems.second.data(), pems.second.size(), key_path);
    _tls = make_shared<r_ssl_server_config>(pems.first, pems.second);
    R_LOG_INFO("r_ws: generated new TLS certificate, fingerprint %s", _tls->fingerprint_sha256().c_str());
}

string r_ws::get_tls_fingerprint() const
{
    return _tls ? _tls->fingerprint_sha256() : string();
}

string r_ws::get_tls_cert_pem() const
{
    return _tls ? _tls->cert_pem() : string();
}

bool r_ws::_read_remote_access(const string& top_dir)
{
    auto path = top_dir + PATH_SLASH + "web_settings.json";
    try
    {
        if(!r_fs::file_exists(path))
            return false;
        auto bytes = r_fs::read_file(path);
        auto j = json::parse(string(bytes.begin(), bytes.end()));
        return j.is_object() && j.value("remote_access", false);
    }
    catch(const exception& ex)
    {
        R_LOG_WARNING("r_ws: ignoring unreadable %s (remote access stays off): %s", path.c_str(), ex.what());
        return false;
    }
}

bool r_ws::remote_access_configured() const
{
    return _read_remote_access(_top_dir);
}

bool r_ws::remote_access_active() const
{
    return _remote_access_active;
}

void r_ws::set_remote_access(bool enabled)
{
    auto path = _top_dir + PATH_SLASH + "web_settings.json";
    json j = json::object();
    try
    {
        if(r_fs::file_exists(path))
        {
            auto bytes = r_fs::read_file(path);
            auto existing = json::parse(string(bytes.begin(), bytes.end()));
            if(existing.is_object())
                j = existing;
        }
    }
    catch(...) {}
    j["remote_access"] = enabled;
    auto text = j.dump(2);
    r_fs::atomic_write_file((const uint8_t*)text.data(), text.size(), path);
    R_LOG_INFO("r_ws: remote access set %s (applies when Revere restarts)", enabled ? "on" : "off");
}

// Clamp a caller-supplied snapshot dimension into something the decode/encode
// path can actually handle. s_to_uint16() yields 0 for a missing, non-numeric
// or zero argument, and 0 propagates all the way into
// av_image_get_buffer_size(), which answers with a negative error that then
// gets read as an enormous unsigned allocation. These values arrive from HTTP
// query args — including snapshot requests relayed from the cloud, whose width
// and height come off the wire — so treat them as untrusted.
static uint16_t _clamp_snapshot_dim(uint16_t v, uint16_t fallback)
{
    constexpr uint16_t MIN_DIM = 16;
    constexpr uint16_t MAX_DIM = 8192;   // beyond any camera we support

    if(v == 0)
        return fallback;
    if(v < MIN_DIM)
        return MIN_DIM;
    if(v > MAX_DIM)
        return MAX_DIM;
    return v;
}

// /jpg calls at least this slow are logged with their time.
static constexpr long long SLOW_JPG_MS = 2000;

r_http::r_server_response r_ws::_get_jpg(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("camera_id") == end(args))
            R_THROW(("Missing camera_id."));

        if(args.find("start_time") == end(args))
            R_THROW(("Missing start_time."));

        // w/h are a bounding box (query_get_jpg preserves aspect ratio inside it).
        // long_edge=N is shorthand for an NxN box, i.e. "cap the longer edge at N"
        // regardless of camera orientation.
        uint16_t w = 640, h = 480;
        if(args.find("long_edge") != end(args))
        {
            uint16_t le = _clamp_snapshot_dim(r_string_utils::s_to_uint16(args["long_edge"]), 640);
            w = le;
            h = le;
        }
        else
        {
            if(args.find("width") != end(args))
                w = _clamp_snapshot_dim(r_string_utils::s_to_uint16(args["width"]), 640);
            if(args.find("height") != end(args))
                h = _clamp_snapshot_dim(r_string_utils::s_to_uint16(args["height"]), 480);
        }

        auto t_start = steady_clock::now();
        int64_t frame_ts = 0;
        auto result = query_get_jpg(
            _top_dir,
            _devices,
            args["camera_id"],
            r_time_utils::iso_8601_to_tp(args["start_time"]),
            w,
            h,
            &frame_ts
        );

        // Only the slow ones: /jpg is called for every event frame and snapshot.
        auto elapsed_ms = (long long)duration_cast<milliseconds>(steady_clock::now() - t_start).count();
        if(elapsed_ms >= SLOW_JPG_MS)
            R_LOG_INFO("[JPG] %s at %s (%ux%u): %lld ms", args["camera_id"].c_str(), args["start_time"].c_str(), w, h, elapsed_ms);

        r_server_response response;
        response.set_content_type("image/jpeg");
        response.set_body(result.size(), result.data());
        // The key frame decoded, which can differ from start_time: callers record which frame they got.
        response.add_additional_header("X-Frame-Ts", std::to_string(frame_ts));
        return response;
    }
    catch(const r_utils::r_not_found_exception&)
    {
        throw; // no recording for this camera (yet): 404, not a logged 500
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to create jpg."));
}

r_http::r_server_response r_ws::_get_webp(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("camera_id") == end(args))
            R_THROW(("Missing camera_id."));

        if(args.find("start_time") == end(args))
            R_THROW(("Missing start_time."));

        // w/h are a bounding box (query_get_webp preserves aspect ratio inside it).
        // long_edge=N is shorthand for an NxN box, i.e. "cap the longer edge at N"
        // regardless of camera orientation.
        uint16_t w = 640, h = 480;
        if(args.find("long_edge") != end(args))
        {
            uint16_t le = _clamp_snapshot_dim(r_string_utils::s_to_uint16(args["long_edge"]), 640);
            w = le;
            h = le;
        }
        else
        {
            if(args.find("width") != end(args))
                w = _clamp_snapshot_dim(r_string_utils::s_to_uint16(args["width"]), 640);
            if(args.find("height") != end(args))
                h = _clamp_snapshot_dim(r_string_utils::s_to_uint16(args["height"]), 480);
        }

        int64_t frame_ts = 0;
        auto result = query_get_webp(
            _top_dir,
            _devices,
            args["camera_id"],
            r_time_utils::iso_8601_to_tp(args["start_time"]),
            w,
            h,
            &frame_ts
        );

        r_server_response response;
        response.set_content_type("image/webp");
        response.set_body(result.size(), result.data());
        // The key frame decoded, which can differ from start_time: callers record which frame they got.
        response.add_additional_header("X-Frame-Ts", std::to_string(frame_ts));
        return response;
    }
    catch(const r_utils::r_not_found_exception&)
    {
        throw; // no recording for this camera (yet): 404, not a logged 500
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to create webp."));
}

r_http::r_server_response r_ws::_get_key_frame(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("camera_id") == end(args))
            R_THROW(("Missing camera_id."));

        if(args.find("start_time") == end(args))
            R_THROW(("Missing start_time."));

        auto result = query_get_key_frame(
            _top_dir,
            _devices,
            args["camera_id"],
            r_time_utils::iso_8601_to_tp(args["start_time"])
        );

        r_server_response response;
        response.set_content_type("application/octet-stream");
        response.set_body(result.size(), result.data());
        return response;
    }
    catch(const r_utils::r_not_found_exception&)
    {
        throw; // no recording for this camera (yet): 404, not a logged 500
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to fetch key frame."));
}

r_http::r_server_response r_ws::_get_contents(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        auto start_time_s = args["start_time"];

        bool input_z_time = start_time_s.find("Z") != std::string::npos;

        if(args.find("end_time") == args.end())
            R_THROW(("Missing end_time."));
        
        auto end_time_s = args["end_time"];

        auto contents = query_get_contents(
            _top_dir,
            _devices,
            args["camera_id"],
            r_time_utils::iso_8601_to_tp(start_time_s),
            r_time_utils::iso_8601_to_tp(end_time_s)            
        );

        json j;
        j["first_ts"] = contents.first_ts.is_null() ? "" : r_time_utils::tp_to_iso_8601(contents.first_ts.value(), input_z_time);
        j["last_ts"]  = contents.last_ts.is_null()  ? "" : r_time_utils::tp_to_iso_8601(contents.last_ts.value(),  input_z_time);
        j["segments"] = json::array();

        for(auto& s : contents.segments)
        {
            j["segments"].push_back({{"start_time", r_time_utils::tp_to_iso_8601(s.start, input_z_time)},
                                    {"end_time", r_time_utils::tp_to_iso_8601(s.end, input_z_time)}});
        }

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const r_utils::r_not_found_exception&)
    {
        throw; // no recording for this camera (yet): 404, not a logged 500
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to get contents."));
}

// Which build this is, for the web UI, the cloud plugin and support:
//   {"version": "0.0.31-dirty", "build_number": 273, "build_time": "2026-10-15T20:01:02Z",
//    "git_revision": "abc1234", "plugins": [{"name": "...", "version": "0.9.2"}]}
// A plugin that doesn't report its version has "version": null. No secrets,
// so no auth, like /auth_status.
r_http::r_server_response r_ws::_get_version(r_utils::r_socket_base&, const r_http::r_server_request&)
{
    try
    {
        json plugins = json::array();
        for(const auto& name : _stream_keeper.get_loaded_system_plugins())
        {
            auto v = _stream_keeper.get_system_plugin_version(name);
            plugins.push_back({{"name", name}, {"version", v.empty() ? json(nullptr) : json(v)}});
        }

        json j = {
            {"version", revere_version()},
            {"build_number", revere_build_number()},
            {"build_time", revere_build_time()},
            {"git_revision", revere_git_revision()},
            {"plugins", plugins}
        };

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to get version."));
}

r_http::r_server_response r_ws::_get_cameras(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto cameras = query_get_cameras(_devices);

        // Live recording health, keyed by camera id (assigned cameras only). Lets
        // the UI show a real connected/not-connected indicator like the desktop.
        std::map<std::string, r_stream_status> status_by_id;
        for(auto& s : _stream_keeper.fetch_stream_status())
            status_by_id[s.camera.id] = s;

        json j;
        j["cameras"] = json::array();

        for(auto c : cameras)
        {
            bool do_motion_detection = (c.do_motion_detection.is_null())?false:c.do_motion_detection.value();
            auto src = _camera_source_info(_top_dir, c);
            const auto& res = src.resolution;

            bool receiving_video = false;
            bool stream_failed = false;
            int64_t storage_write_lag_ms = 0;
            int64_t storage_timeline_lag_ms = 0;
            json camera_events = nullptr;
            json motion_tier = nullptr;
            json ingest_lag = nullptr;
            auto sit = status_by_id.find(c.id);
            if(sit != status_by_id.end())
            {
                receiving_video = sit->second.receiving_video;
                stream_failed = sit->second.failed;
                storage_write_lag_ms = sit->second.storage_write_lag_ms;
                storage_timeline_lag_ms = sit->second.storage_timeline_lag_ms;
                if(sit->second.has_camera_events)
                {
                    const auto& oe = sit->second.camera_events;
                    camera_events = {
                        {"source", oe.source},
                        {"up", oe.up},
                        {"state", oe.state},
                        {"port", oe.port},
                        {"motion_topic", oe.motion_topic},
                        {"classes", oe.classes},
                        {"last_event_ms", oe.last_event_ms},
                        {"failures", oe.failures},
                        {"last_error", oe.last_error}
                    };
                }
                const auto& il = sit->second.ingest_lag;
                ingest_lag = {
                    {"lag_ms", il.lag_ms},
                    {"behind_for_ms", il.behind_for_ms},
                    {"rate", il.rate},
                    {"lagging", il.lagging},
                    {"episodes", il.episodes},
                    {"lag_reconnects_1h", sit->second.lag_reconnects_1h},
                    {"falling_behind", sit->second.falling_behind},
                    {"message", sit->second.falling_behind ? LAG_FLAG_MESSAGE : ""}
                };
                const auto& mt = sit->second.motion_tier;
                motion_tier = {
                    {"tier", mt.tier},
                    {"automatic", mt.automatic},
                    {"reason", mt.reason},
                    {"checked", mt.checked},
                    {"matched", mt.matched},
                    {"person_checked", mt.person_checked},
                    {"person_matched", mt.person_matched}
                };
            }

            // An address change for this camera, held (waiting for the user, or
            // to be followed) or followed; null when none.
            json camera_move = nullptr;
            auto move_n = _devices.get_camera_move(c.id);
            if(!move_n.is_null())
            {
                const auto& mv = move_n.value();
                camera_move = {
                    {"status", mv.status},
                    {"old_ipv4", mv.old_ipv4},
                    {"new_ipv4", mv.new_ipv4},
                    {"old_port", mv.old_port},
                    {"new_port", mv.new_port},
                    {"seen_at_ms", mv.seen_at_ms},
                    {"old_dead_since_ms", mv.old_dead_since_ms},
                    {"followed_at_ms", mv.followed_at_ms},
                    {"message", r_disco::camera_move_message(mv)}
                };
            }

            j["cameras"].push_back(
                {
                    {"id", c.id},
                    {"camera_name", (c.camera_name.is_null())?"":c.camera_name.value()},
                    {"friendly_name", (c.friendly_name.is_null())?"":c.friendly_name.value()},
                    {"ipv4", (c.ipv4.is_null())?"":c.ipv4.value()},
                    {"manufacturer", (c.manufacturer.is_null())?"":c.manufacturer.value()},
                    {"model", (c.model.is_null())?"":c.model.value()},
                    {"firmware_version", (c.firmware_version.is_null())?"":c.firmware_version.value()},
                    {"rtsp_url", (c.rtsp_url.is_null())?"":c.rtsp_url.value()},
                    {"video_codec", (c.video_codec.is_null())?"":c.video_codec.value()},
                    {"audio_codec", (c.audio_codec.is_null())?"":c.audio_codec.value()},
                    {"state", c.state},
                    {"do_motion_detection", do_motion_detection},
                    {"do_motion_pruning", (c.do_motion_pruning.is_null())?false:c.do_motion_pruning.value()},
                    {"min_continuous_recording_hours", (c.min_continuous_recording_hours.is_null())?24:c.min_continuous_recording_hours.value()},
                    {"record_file_path", (c.record_file_path.is_null())?"":c.record_file_path.value()},
                    {"receiving_video", receiving_video},
                    {"storage_write_lag_ms", storage_write_lag_ms},
                    {"storage_timeline_lag_ms", storage_timeline_lag_ms},
                    {"stream_failed", stream_failed},
                    {"manual", (c.xaddrs.is_null() && !c.rtsp_url.is_null())},
                    {"width", res.is_null() ? 0 : res.value().first},
                    {"height", res.is_null() ? 0 : res.value().second},
                    // 0 / "" when unknown, like width/height.
                    {"fps", src.framerate.is_null() ? 0.0 : (double)src.framerate.value()},
                    {"onvif_profile", (c.onvif_profile_token.is_null())?"":c.onvif_profile_token.value()},
                    // Where the camera's ONVIF device service last answered; 0 / "" when unknown.
                    {"onvif_port", (c.onvif_port.is_null())?0:c.onvif_port.value()},
                    {"onvif_protocol", (c.onvif_protocol.is_null())?"":c.onvif_protocol.value()},
                    // The camera's ONVIF event subscription; null when it has none.
                    {"camera_events", camera_events},
                    // An address change (r_disco/r_camera_move.h): held or followed; null when none.
                    {"camera_move", camera_move},
                    // "auto", "1", "2" or "3": where motion and object classes come from.
                    {"motion_tier_setting", (c.motion_tier.is_null())?"auto":c.motion_tier.value()},
                    // The tier in use now and why; null while not recording.
                    {"motion_tier", motion_tier},
                    // How far its frames reach us behind live; null while not recording.
                    {"ingest_lag", ingest_lag}
                }
            );
        }

        // This build's version (git describe), so clients such as the cloud
        // plugin can report it. /version has the build number and time too.
        j["version"] = revere_version();

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to get cameras."));
}

static float _compute_framerate(const r_blob_tree& bt)
{
    if(!bt.has_key("frames"))
        R_THROW(("Blob tree missing frames array."));

    auto frame_count = bt.at("frames").size();

    int64_t last_video_ts = 0;
    bool has_last_video_ts = false;;

    vector<int64_t> deltas;

    for(size_t fi = 0; fi < frame_count; ++fi)
    {
        if(!bt.at("frames").has_index(fi))
            R_THROW(("Blob tree missing frame."));

        if(!bt.at("frames").at(fi).has_key("stream_id"))
            R_THROW(("Blob tree missing stream_id."));

        auto stream_id = bt.at("frames").at(fi).at("stream_id").get_value<int>();
        if(stream_id == r_storage::R_STORAGE_MEDIA_TYPE_VIDEO)
        {
            if(!bt.at("frames").at(fi).has_key("ts"))
                R_THROW(("Blob tree missing ts."));

            auto ts = bt.at("frames").at(fi).at("ts").get_value<int64_t>();

            if(has_last_video_ts && (ts > last_video_ts))
                deltas.push_back(ts - last_video_ts);

            last_video_ts = ts;
            has_last_video_ts = true;
        }
    }

    // Need at least one inter-frame delta to average; otherwise the division
    // below is a divide-by-zero (SIGFPE). Reachable when a chunk has 0 or 1 video
    // frames, or non-increasing timestamps.
    if(deltas.empty())
        R_THROW(("Cannot compute framerate: no usable video frame deltas."));

    int64_t avg_delta = (std::accumulate(begin(deltas), end(deltas), (int64_t)0, [](int64_t a, int64_t b) {return a + b;}) / deltas.size());

    return (float)1000 / (float)avg_delta;
}

static void _check_timestamps(const r_blob_tree& bt)
{
    if(!bt.has_key("frames"))
        R_THROW(("Blob tree missing frames array."));

    auto frame_count = bt.at("frames").size();

    int64_t last_video_ts = 0;
    bool has_last_video_ts = false;;

    for(size_t fi = 0; fi < frame_count; ++fi)
    {
        if(!bt.at("frames").has_index(fi))
            R_THROW(("Blob tree missing frame."));

        if(!bt.at("frames").at(fi).has_key("stream_id"))
            R_THROW(("Blob tree missing stream_id."));

        auto stream_id = bt.at("frames").at(fi).at("stream_id").get_value<int>();
        if(stream_id == r_storage::R_STORAGE_MEDIA_TYPE_VIDEO)
        {
            if(!bt.at("frames").at(fi).has_key("ts"))
                R_THROW(("Blob tree missing ts."));

            auto ts = bt.at("frames").at(fi).at("ts").get_value<int64_t>();

            if(has_last_video_ts)
            {
                if(ts < last_video_ts)
                    R_THROW(("Timestamp is not monotonically increasing."));
            }

            last_video_ts = ts;
            has_last_video_ts = true;
        }
    }
}

void r_ws::_export_entry_point()
{
    while(true)
    {
        auto item = _export_q.poll();
        if(item.is_null())
            break;

        auto job = item.value();
        auto job_id = job.id;

        bool failed = false;
        try
        {
            if(job.type == export_type::standard)
                _export(job);
            else
                _transcode_export(job);
        }
        catch(const exception& ex)
        {
            failed = true;
            R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
        }

        {
            lock_guard<mutex> lock(_export_progress_mutex);
            auto it = _export_progress.find(job_id);
            if(it != _export_progress.end())
            {
                it->second.percent_complete = 100;
                it->second.completed_at = steady_clock::now();
                it->second.failed = failed;
            }
            auto cutoff = steady_clock::now() - minutes(5);
            for(auto it2 = _export_progress.begin(); it2 != _export_progress.end(); )
            {
                if(it2->second.percent_complete == 100 && it2->second.completed_at < cutoff)
                    it2 = _export_progress.erase(it2);
                else
                    ++it2;
            }
        }
    }
}

r_http::r_server_response r_ws::_get_export(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto exports_path = _top_dir + PATH_SLASH + "exports";
        if(!r_fs::file_exists(exports_path))
            r_fs::mkdir(exports_path);

        auto args = request.get_uri().get_get_args();

        if(args.find("camera_id") == args.end())
            R_THROW(("Missing camera_id."));
        if(args.find("start_time") == args.end())
            R_THROW(("Missing start_time."));
        if(args.find("end_time") == args.end())
            R_THROW(("Missing end_time."));
        if(args.find("file_name") == args.end())
            R_THROW(("Missing file name."));
        if(!is_safe_export_filename(args["file_name"]))
            R_THROW(("Invalid file_name."));

        export_job job;
        job.id = r_uuid::generate();
        job.type = export_type::standard;
        job.camera_id = args["camera_id"];
        job.start_time = r_time_utils::iso_8601_to_tp(args["start_time"]);
        job.end_time = r_time_utils::iso_8601_to_tp(args["end_time"]);
        job.file_name = args["file_name"];
        job.exports_path = exports_path;

        {
            lock_guard<mutex> lock(_export_progress_mutex);

            size_t inflight = 0;
            for(const auto& kv : _export_progress)
                if(kv.second.percent_complete < 100) ++inflight;
            if(inflight >= MAX_INFLIGHT_EXPORTS)
                R_THROW(("Too many exports in progress; try again later."));

            auto& p = _export_progress[job.id];
            p.percent_complete = 0;
            p.completed_at = {};
            p.file_path = exports_path + PATH_SLASH + job.file_name;
        }

        _export_q.post(job);

        json j;
        j["id"] = job.id;

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to enqueue export."));
}

void r_ws::_export(export_job& job)
{
    auto dot = job.file_name.rfind('.');
    auto ext = (dot != string::npos) ? job.file_name.substr(dot) : string{};
    auto output_path = job.exports_path + PATH_SLASH + job.file_name;
    auto temp_path = r_fs::temp_file_name(job.exports_path, "tmp_") + ext;

    r_muxer muxer(temp_path);
    muxer.enable_faststart();

    auto qs = job.start_time;
    auto qe = job.end_time;

    bool muxer_opened = false;
    int64_t ts_first_frame = 0;
    bool done = false;

    while(!done)
    {
        if(!_export_running) break;

        auto rs = qs;
        auto re = rs;
        if(rs + chrono::minutes(5) >= qe)
        {
            re = qe;
            done = true;
        }
        else re = rs + chrono::minutes(5);

        auto qr_buffer = query_get_video(_top_dir, _devices, job.camera_id, rs, re);
        qs = re;

        {
            lock_guard<mutex> lock(_export_progress_mutex);
            auto total_ms = duration_cast<milliseconds>(job.end_time - job.start_time).count();
            auto done_ms  = duration_cast<milliseconds>(qs - job.start_time).count();
            auto pit = _export_progress.find(job.id);
            if(pit != _export_progress.end())
                pit->second.percent_complete = (total_ms > 0) ? (int)(done_ms * 100 / total_ms) : 100;
        }

        try
        {
            uint32_t version = 0;
            auto bt = r_blob_tree::deserialize(qr_buffer.data(), qr_buffer.size(), version);

            _check_timestamps(bt);

            if(!muxer_opened)
            {
                if(!bt.has_key("has_audio"))
                    R_THROW(("Blob tree missing audio indicator."));

                bool has_audio = (bt["has_audio"].get_string() == "true")?true:false;

                if(!bt.has_key("video_codec_name"))
                    R_THROW(("Blob tree missing video codec name."));

                auto video_codec_name = bt["video_codec_name"].get_string();

                if(!bt.has_key("video_codec_parameters"))
                    R_THROW(("Blob tree missing video codec parameters."));

                auto video_codec_parameters = bt["video_codec_parameters"].get_string();

                string audio_codec_name, audio_codec_parameters;
                if(has_audio)
                {
                    if(!bt.has_key("audio_codec_name"))
                        R_THROW(("Blob tree missing audio codec name but has audio!"));
                    audio_codec_name = bt["audio_codec_name"].get_string();

                    if(!bt.has_key("audio_codec_parameters"))
                        R_THROW(("Blob tree missing audio codec parameters but has audio!"));
                    audio_codec_parameters = bt["audio_codec_parameters"].get_string();
                }

                auto fr = r_pipeline::get_video_codec_framerate(video_codec_parameters);

                if(fr.is_null())
                    fr.set_value(_compute_framerate(bt));

                auto video_codec_id = r_av::encoding_to_av_codec_id(video_codec_name);

                if(video_codec_id == AV_CODEC_ID_H264)
                {
                    auto maybe_sps = r_pipeline::get_h264_sps(video_codec_parameters);
                    if(!maybe_sps.is_null())
                    {
                        auto sps_info = r_pipeline::parse_h264_sps(maybe_sps.value());
                        muxer.add_video_stream(av_d2q(fr, 10000), video_codec_id, sps_info.width, sps_info.height, sps_info.profile_idc, sps_info.level_idc);
                    }
                    auto maybe_pps = r_pipeline::get_h264_pps(video_codec_parameters);
                    muxer.set_video_extradata(r_pipeline::make_h264_extradata(maybe_sps, maybe_pps));
                }
                else if(video_codec_id == AV_CODEC_ID_HEVC)
                {
                    auto maybe_vps = r_pipeline::get_h265_vps(video_codec_parameters);
                    auto maybe_sps = r_pipeline::get_h265_sps(video_codec_parameters);
                    if(!maybe_sps.is_null())
                    {
                        auto sps_info = r_pipeline::parse_h265_sps(maybe_sps.value());
                        muxer.add_video_stream(av_d2q(fr, 10000), video_codec_id, sps_info.width, sps_info.height, sps_info.profile_idc, sps_info.level_idc);
                    }
                    auto maybe_pps = r_pipeline::get_h265_pps(video_codec_parameters);
                    muxer.set_video_extradata(r_pipeline::make_h265_extradata(maybe_vps, maybe_sps, maybe_pps));
                }

                if(has_audio)
                {
                    r_nullable<int> audio_rate, audio_channels;
                    auto audio_codec_parameter_parts = r_string_utils::split(audio_codec_parameters, ",");
                    for(auto part : audio_codec_parameter_parts)
                    {
                        auto inner_parts = r_string_utils::split(part, "=");
                        if(inner_parts.size() == 2)
                        {
                            if(r_string_utils::strip(inner_parts[0]) == "sc_audio_rate")
                                audio_rate.set_value(r_string_utils::s_to_int(inner_parts[1]));
                            if(r_string_utils::strip(inner_parts[0]) == "sc_audio_channels")
                                audio_channels.set_value(r_string_utils::s_to_int(inner_parts[1]));
                        }
                    }

                    auto audio_codec_id = r_av::encoding_to_av_codec_id(audio_codec_name);

                    if(audio_channels.is_null())
                        audio_channels.set_value(1);

                    if(audio_rate.is_null())
                    {
                        if(audio_codec_id == AV_CODEC_ID_PCM_MULAW)
                            audio_rate.set_value(8000);
                        else if(audio_codec_id == AV_CODEC_ID_PCM_ALAW)
                            audio_rate.set_value(8000);
                    }

                    if(audio_rate.is_null())
                        R_THROW(("Missing audio rate."));

                    muxer.add_audio_stream(audio_codec_id, (uint8_t)audio_channels.value(), (uint16_t)audio_rate.value());
                }

                muxer.open();
                muxer_opened = true;
            }

            if(!bt.has_key("frames"))
                R_THROW(("Blob tree missing frames."));

            auto n_frames = bt["frames"].size();
            for(size_t fi = 0; fi < n_frames; ++fi)
            {
                if(!bt["frames"].has_index(fi))
                    R_THROW(("Blob tree missing frame."));
                if(!bt["frames"][fi].has_key("stream_id"))
                    R_THROW(("Blob tree missing stream id."));

                auto sid = bt["frames"][fi]["stream_id"].get_value<int>();

                if(!bt["frames"][fi].has_key("key"))
                    R_THROW(("Blob tree missing key."));

                auto key = (bt["frames"][fi]["key"].get_string() == "true");

                if(!bt["frames"][fi].has_key("data"))
                    R_THROW(("Blob tree missing data."));

                auto frame = bt["frames"][fi]["data"].get_blob();

                if(!bt["frames"][fi].has_key("ts"))
                    R_THROW(("Blob tree missing ts."));

                auto ts = bt["frames"][fi]["ts"].get_value<int64_t>();

                if(ts_first_frame == 0)
                    ts_first_frame = ts;

                if(sid == R_STORAGE_MEDIA_TYPE_VIDEO)
                    muxer.write_video_frame(frame.data(), frame.size(), ts-ts_first_frame, ts-ts_first_frame, {1, 1000}, key);
                else if(sid == R_STORAGE_MEDIA_TYPE_AUDIO)
                    muxer.write_audio_frame(frame.data(), frame.size(), ts-ts_first_frame, {1, 1000});
            }
        }
        catch(const exception& ex)
        {
            R_LOG_WARNING("_export(): skipping chunk [%s -> %s]: %s",
                r_time_utils::tp_to_iso_8601(rs, false).c_str(),
                r_time_utils::tp_to_iso_8601(re, false).c_str(),
                ex.what()
            );
        }
    }

    if(muxer_opened)
    {
        muxer.finalize();
        r_fs::atomic_rename_file(temp_path, output_path);
    }
}

r_http::r_server_response r_ws::_get_analytics(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("camera_id") == args.end())
            R_THROW(("Missing camera_id."));

        if(args.find("start_time") == args.end())
            R_THROW(("Missing start_time."));

        auto start_time_s = args["start_time"];
        auto start_tp = r_time_utils::iso_8601_to_tp(start_time_s);

        if(args.find("end_time") == args.end())
            R_THROW(("Missing end_time."));

        auto end_time_s = args["end_time"];
        auto end_tp = r_time_utils::iso_8601_to_tp(end_time_s);

        // Check for optional stream_tag parameter
        r_nullable<string> stream_tag;
        if(args.find("stream_tag") != args.end()) {
            stream_tag.set_value(args["stream_tag"]);
        }

        auto analytics_data = query_get_analytics(_top_dir, _devices, args["camera_id"], start_tp, end_tp, stream_tag);

        json j;
        j["analytics"] = json::array();

        for(const auto& entry : analytics_data)
        {
            // Parse the JSON data and extract the analytics object
            try {
                auto parsed = json::parse(entry.json_data);
                if(parsed.contains("analytics")) {
                    j["analytics"].push_back(parsed["analytics"]);
                }
            } catch(const exception&) {
                // Skip entries that don't parse or don't contain analytics
            }
        }

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to query analytics."));
}


// GET /camera_events?camera_id=<id>[&since_ms=<epoch ms>]: the camera's own
// event spans (motion, person, vehicle, ...) on Revere's clock, as its ONVIF
// subscription reported them (shadow mode, kept in memory, newest 500). For
// comparing with /motion_events.
r_http::r_server_response r_ws::_get_camera_events(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();
        if(args.find("camera_id") == args.end())
            R_THROW(("Missing camera_id."));
        int64_t since_ms = 0;
        if(args.find("since_ms") != args.end())
            since_ms = r_string_utils::s_to_int64(args["since_ms"]);

        json j;
        j["camera_events"] = json::array();
        for(auto& s : _stream_keeper.camera_spans(args["camera_id"], since_ms))
            j["camera_events"].push_back({{"what", s.what}, {"start_ms", s.start_ms}, {"end_ms", s.end_ms}});

        // The detection pass's sessions over those spans (boxes normalised like analytics).
        auto dets_json = [](const std::vector<r_detection>& dets) {
            json a = json::array();
            for(auto& d : dets)
                a.push_back({{"class_name", d.class_name}, {"confidence", d.confidence}, {"ts", d.ts},
                             {"x1", d.x1 / 640.0f}, {"y1", d.y1 / 640.0f}, {"x2", d.x2 / 640.0f}, {"y2", d.y2 / 640.0f}});
            return a;
        };
        j["detection_sessions"] = json::array();
        for(auto& s : _stream_keeper.detection_sessions(args["camera_id"], since_ms))
            j["detection_sessions"].push_back({
                {"start_ms", s.start_ms},
                {"end_ms", s.end_ms},
                {"camera_classes", s.camera_classes},
                {"looks", s.looks},
                {"failed_looks", s.failed_looks},
                {"dropped_static", s.dropped_static},
                {"first_found_ms", s.first_found_ms},
                {"first_found", dets_json(s.first_found)},
                {"detections", dets_json(s.detections)}
            });

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to get ONVIF events."));
}

r_http::r_server_response r_ws::_get_motion_events(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("start_time") == args.end())
            R_THROW(("Missing start_time."));

        auto start_time_s = args["start_time"];

        auto start_tp = r_time_utils::iso_8601_to_tp(start_time_s);

        bool input_z_time = start_time_s.find("Z") != std::string::npos;

        if(args.find("end_time") == args.end())
            R_THROW(("Missing end_time."));
        
        auto end_time_s = args["end_time"];

        auto end_tp = r_time_utils::iso_8601_to_tp(end_time_s);

        auto motion_events = query_get_motion_events(_top_dir, _devices, args["camera_id"], start_tp, end_tp);

        json j;
        j["motion_events"] = json::array();

        for(auto e : motion_events)
        {
            json j_motion;

            j_motion["start_time"] = r_time_utils::tp_to_iso_8601(e.start, input_z_time);
            j_motion["end_time"] = r_time_utils::tp_to_iso_8601(e.end, input_z_time);
            j_motion["motion"] = e.motion;
            j_motion["avg_motion"] = e.avg_motion;
            j_motion["stddev"] = e.stddev;

            j["motion_events"].push_back(j_motion);
        }

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to query motions."));
}

r_http::r_server_response r_ws::_get_video(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("camera_id") == args.end())
            R_THROW(("Missing camera_id."));

        if(args.find("start_time") == args.end())
            R_THROW(("Missing start_time."));

        if(args.find("end_time") == args.end())
            R_THROW(("Missing end_time."));

        auto qr_buffer = query_get_video(
            _top_dir,
            _devices,
            args["camera_id"],
            r_time_utils::iso_8601_to_tp(args["start_time"]),
            r_time_utils::iso_8601_to_tp(args["end_time"])
        );

        r_server_response response;
        response.set_content_type("application/vnd.revere.blobtree.v1");
        response.set_body(qr_buffer.size(), qr_buffer.data());
        return response;
    }
    catch(const r_utils::r_not_found_exception&)
    {
        throw; // no recording for this camera (yet): 404, not a logged 500
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to get video."));
}


void r_ws::_evict_oldest_transcode_session()
{
    if(_sessions.empty()) return;

    auto oldest = _sessions.begin();
    for(auto it = next(_sessions.begin()); it != _sessions.end(); ++it)
    {
        if(it->second->last_used < oldest->second->last_used)
            oldest = it;
    }
    _sessions.erase(oldest);
}

r_http::r_server_response r_ws::_get_create_transcode_stream(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("width") == args.end())
            R_THROW(("Missing width."));
        if(args.find("height") == args.end())
            R_THROW(("Missing height."));
        if(args.find("bitrate") == args.end())
            R_THROW(("Missing bitrate."));
        if(args.find("framerate_num") == args.end())
            R_THROW(("Missing framerate_num."));
        if(args.find("framerate_den") == args.end())
            R_THROW(("Missing framerate_den."));

        // codec defaults to "h264"; "h265" is also supported.
        string output_codec = "h264";
        if(args.find("codec") != args.end())
            output_codec = r_string_utils::to_lower(args["codec"]);

        if(output_codec != "h264" && output_codec != "h265" && output_codec != "hevc")
            R_THROW(("Unsupported output codec: %s", output_codec.c_str()));

        auto session = make_shared<r_transcode_session>();
        session->output_width      = r_string_utils::s_to_uint16(args["width"]);
        session->output_height     = r_string_utils::s_to_uint16(args["height"]);
        session->bitrate           = r_string_utils::s_to_uint32(args["bitrate"]);
        session->framerate_num     = r_string_utils::s_to_uint32(args["framerate_num"]);
        session->framerate_den     = r_string_utils::s_to_uint32(args["framerate_den"]);
        session->output_codec_name = output_codec;
        session->codec_initialized = false;
        session->last_used         = steady_clock::now();

        if(args.find("audio_sample_rate") != args.end())
            session->target_audio_sample_rate = r_string_utils::s_to_int(args["audio_sample_rate"]);
        if(args.find("audio_channels") != args.end())
            session->target_audio_channels = r_string_utils::s_to_int(args["audio_channels"]);

        auto handle = r_utils::r_uuid::generate();

        {
            lock_guard<mutex> lock(_sessions_mutex);
            if(_sessions.size() >= MAX_TRANSCODE_SESSIONS)
                _evict_oldest_transcode_session();
            _sessions[handle] = move(session);
        }

        json j;
        j["handle"] = handle;

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to create transcode stream."));
}

// Scan an Annex-B H.264 bitstream and return the first NAL unit of the given type,
// prefixed with a 4-byte start code (00 00 00 01). Returns empty if not found.
static vector<uint8_t> _extract_annexb_nalu(const uint8_t* data, size_t size, uint8_t target_type)
{
    if(!data || size < 4) return {};

    // Collect (nal_data_start, nal_data_end) pairs by scanning for 00 00 01 patterns.
    vector<pair<size_t,size_t>> nals;
    size_t i = 0;
    while(i + 2 < size)
    {
        if(data[i] == 0 && data[i+1] == 0 && data[i+2] == 1)
        {
            size_t nal_start = i + 3;
            if(!nals.empty())
            {
                // Close previous NAL, trimming any trailing zero bytes (part of next start code).
                size_t nal_end = i;
                while(nal_end > nals.back().first && data[nal_end - 1] == 0)
                    --nal_end;
                nals.back().second = nal_end;
            }
            nals.push_back({nal_start, size});
            i = nal_start;
        }
        else ++i;
    }

    for(size_t n = 0; n < nals.size(); ++n)
    {
        size_t ns = nals[n].first;
        size_t ne = nals[n].second;
        if(ns < ne && ns < size && (data[ns] & 0x1F) == target_type)
        {
            vector<uint8_t> result;
            result.push_back(0); result.push_back(0); result.push_back(0); result.push_back(1);
            result.insert(result.end(), data + ns, data + ne);
            return result;
        }
    }
    return {};
}

r_http::r_server_response r_ws::_get_transcode(r_utils::r_socket_base& conn, const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("handle") == args.end())
            R_THROW(("Missing handle."));
        if(args.find("camera_id") == args.end())
            R_THROW(("Missing camera_id."));
        if(args.find("start_time") == args.end())
            R_THROW(("Missing start_time."));
        if(args.find("end_time") == args.end())
            R_THROW(("Missing end_time."));

        auto handle    = args["handle"];
        auto start_tp  = r_time_utils::iso_8601_to_tp(args["start_time"]);
        auto end_tp    = r_time_utils::iso_8601_to_tp(args["end_time"]);
        // Pre-roll frames (ts < start_ms) are needed to prime the decoder but must
        // not appear in the output blob tree — the cloud service uses start_ms as
        // its PTS=0 reference and would clamp earlier frames to 0, causing a frozen
        // still image at the start of the clip.
        auto start_ms  = (int64_t)chrono::duration_cast<chrono::milliseconds>(start_tp.time_since_epoch()).count();

        auto t_start = steady_clock::now();

        // The map lock only to find the session; then the session's own lock for the
        // whole window, so a long transcode never holds up another session's
        // create, transcode or finalize.
        shared_ptr<r_transcode_session> session_ptr;
        {
            lock_guard<mutex> map_lock(_sessions_mutex);
            auto it = _sessions.find(handle);
            if(it == _sessions.end())
                R_THROW(("Unknown transcode handle: %s", handle.c_str()));
            session_ptr = it->second;
            session_ptr->last_used = steady_clock::now();
        }
        lock_guard<mutex> lock(session_ptr->mtx);
        auto& session = *session_ptr;
        auto t_locked = steady_clock::now();

        // Fetch the raw video blobtree for the requested range.
        auto qr_buffer = query_get_video(_top_dir, _devices, args["camera_id"], start_tp, end_tp);
        auto t_read = steady_clock::now();

        uint32_t version = 0;
        auto in_bt = r_blob_tree::deserialize(qr_buffer.data(), qr_buffer.size(), version);

        bool has_audio = in_bt.has_key("has_audio") && (in_bt["has_audio"].get_string() == "true");

        // Lazy-initialize decoder and encoder on the first transcode call.
        if(!session.codec_initialized)
        {
            if(!in_bt.has_key("video_codec_name"))
                R_THROW(("Blob tree missing video_codec_name."));
            if(!in_bt.has_key("video_codec_parameters"))
                R_THROW(("Blob tree missing video_codec_parameters."));

            auto video_codec_name   = in_bt["video_codec_name"].get_string();
            auto video_codec_params = in_bt["video_codec_parameters"].get_string();

            auto codec_id     = r_av::encoding_to_av_codec_id(video_codec_name);
            auto out_codec_id = r_av::encoding_to_av_codec_id(session.output_codec_name);

            auto extradata = r_pipeline::get_video_codec_extradata(video_codec_name, video_codec_params);

            session.decoder = make_unique<r_video_decoder>(codec_id, r_av::r_find_best_hw_accel(codec_id));
            if(!extradata.empty())
                session.decoder->set_extradata(extradata);

            AVRational framerate{(int)session.framerate_num, (int)session.framerate_den};

            bool is_h265 = (session.output_codec_name == "h265" || session.output_codec_name == "hevc");
            int profile = is_h265 ? AV_PROFILE_HEVC_MAIN : AV_PROFILE_H264_MAIN;
            int level   = is_h265 ? 120 : 41;

            session.encoder = make_unique<r_video_encoder>(
                out_codec_id,
                session.bitrate,
                session.output_width,
                session.output_height,
                framerate,
                AV_PIX_FMT_YUV420P,
                0,
                (uint16_t)session.framerate_num,
                profile,
                level,
                "",
                "",
                r_find_best_hw_accel_encoder(out_codec_id)
            );

            float fr = (float)session.framerate_num / (float)session.framerate_den;
            try
            {
                session.output_video_codec_params = r_pipeline::build_video_codec_params(
                    session.output_codec_name, session.encoder->get_extradata(), fr
                );
            }
            catch(const std::exception& ex)
            {
                R_LOG_WARNING("Could not build codec params at encoder init (will retry after first frame): %s", ex.what());
            }

            // Initialize audio transcoding if the input stream has audio.
            if(has_audio && in_bt.has_key("audio_codec_name") && in_bt.has_key("audio_codec_parameters"))
            {
                auto audio_codec_name   = in_bt["audio_codec_name"].get_string();
                auto audio_codec_params = in_bt["audio_codec_parameters"].get_string();
                auto audio_codec_id     = r_av::encoding_to_av_codec_id(audio_codec_name);

                // Parse sample rate and channel count from stored codec parameters.
                r_nullable<int> audio_rate, audio_channels;
                for(auto& part : r_string_utils::split(audio_codec_params, ","))
                {
                    auto kv = r_string_utils::split(part, "=");
                    if(kv.size() != 2) continue;
                    if(r_string_utils::strip(kv[0]) == "sc_audio_rate")
                        audio_rate.set_value(r_string_utils::s_to_int(kv[1]));
                    if(r_string_utils::strip(kv[0]) == "sc_audio_channels")
                        audio_channels.set_value(r_string_utils::s_to_int(kv[1]));
                }
                if(audio_channels.is_null()) audio_channels.set_value(1);
                if(audio_rate.is_null())
                {
                    if(audio_codec_id == AV_CODEC_ID_PCM_MULAW || audio_codec_id == AV_CODEC_ID_PCM_ALAW)
                        audio_rate.set_value(8000);
                }
                if(audio_rate.is_null() && session.target_audio_sample_rate == 0)
                    R_THROW(("Cannot determine audio sample rate for transcoding."));

                // Use caller-requested target params if provided; otherwise use source params.
                int effective_channels = (session.target_audio_channels != 0) ? session.target_audio_channels : audio_channels.value();
                int effective_rate     = (session.target_audio_sample_rate != 0) ? session.target_audio_sample_rate : audio_rate.value();

                session.audio_channels    = effective_channels;
                session.audio_sample_rate = effective_rate;

                session.audio_decoder = make_unique<r_audio_decoder>(audio_codec_id);
                auto input_asc = r_pipeline::get_audio_codec_extradata(audio_codec_params);
                if(!input_asc.empty())
                    session.audio_decoder->set_extradata(input_asc);
                // PCM codecs have no extradata; sample_rate and channels must be set explicitly.
                if(audio_codec_id == AV_CODEC_ID_PCM_MULAW || audio_codec_id == AV_CODEC_ID_PCM_ALAW)
                    session.audio_decoder->set_pcm_params(audio_rate.value(), audio_channels.value());

                // Always transcode to AAC; bitrate scales with channel count.
                uint32_t audio_bitrate = (uint32_t)(64000 * session.audio_channels);
                session.audio_encoder = make_unique<r_audio_encoder>(
                    AV_CODEC_ID_AAC,
                    audio_bitrate,
                    session.audio_sample_rate,
                    session.audio_channels,
                    AV_SAMPLE_FMT_FLTP
                );

                session.audio_channel_buffers.assign(session.audio_channels, {});
                session.output_audio_codec_params = r_pipeline::build_audio_codec_params(
                    session.audio_encoder->get_extradata(),
                    session.audio_sample_rate,
                    session.audio_channels
                );

                session.audio_initialized = true;
                R_LOG_INFO("[TRANSCODE] Audio initialized: %s -> aac, rate=%d, channels=%d, frame_size=%d",
                           audio_codec_name.c_str(), session.audio_sample_rate,
                           session.audio_channels, session.audio_encoder->get_frame_size());
            }

            session.codec_initialized = true;
        }

        // Build the output blobtree with transcoded video and transcoded audio.
        r_blob_tree out_bt;
        out_bt["has_audio"]              = string(session.audio_initialized ? "true" : "false");
        out_bt["video_codec_name"]       = session.output_codec_name;
        out_bt["video_codec_parameters"] = session.output_video_codec_params;

        if(session.audio_initialized)
        {
            out_bt["audio_codec_name"]       = string("aac");
            out_bt["audio_codec_parameters"] = session.output_audio_codec_params;
        }

        size_t n_frames     = in_bt.has_key("frames") ? in_bt["frames"].size() : 0;
        size_t out_frame_idx = 0;

        for(size_t fi = 0; fi < n_frames; ++fi)
        {
            // Cooperative shutdown: abandon a long decode/encode rather than
            // making teardown wait for the whole clip. The connection is being
            // torn down, so the unwound 500 never has to reach the client.
            if(_stopping())
                R_THROW(("Transcode aborted: server shutting down."));
            // Likewise once the client has gone (timed out): nobody will read it.
            if(conn.peer_closed())
                R_THROW(("Transcode aborted: client disconnected."));

            if(!in_bt["frames"].has_index(fi)) continue;

            auto sid = in_bt["frames"][fi]["stream_id"].get_value<int>();
            auto ts  = in_bt["frames"][fi]["ts"].get_value<int64_t>();
            auto& frame_data = in_bt["frames"][fi]["data"].get_blob();

            if(sid == R_STORAGE_MEDIA_TYPE_VIDEO)
            {
                session.decoder->attach_buffer(frame_data.data(), frame_data.size());
                auto dec_state = session.decoder->decode();

                if(dec_state != R_CODEC_STATE_HAS_OUTPUT && dec_state != R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                    continue;

                auto decoded = session.decoder->get(AV_PIX_FMT_YUV420P, session.output_width, session.output_height, 1);

                // Record the first frame's timestamp so we can compute relative positions.
                if(session.first_ts == -1)
                    session.first_ts = ts;

                // Convert relative ms timestamp to encoder timebase units (e.g. 1/30 s for 30fps),
                // using midpoint rounding: pts = (rel_ms * fps_num + 500 * fps_den) / (1000 * fps_den)
                int64_t relative_ts  = ts - session.first_ts;
                int64_t rescaled_pts = (relative_ts  * (int64_t)session.framerate_num
                                        + 500LL       * (int64_t)session.framerate_den)
                                       / (1000LL      * (int64_t)session.framerate_den);

                if(session.next_pts == -1)
                    session.next_pts = rescaled_pts;

                // Drop: frame is behind where the encoder already is — discard it.
                if(rescaled_pts < session.next_pts)
                    continue;

                // Fill: input is ahead — duplicate the last encoded frame into the gap.
                while(rescaled_pts > session.next_pts && session.last_decoded_frame)
                {
                    int64_t dup_ts = session.first_ts
                                     + session.next_pts * 1000LL * (int64_t)session.framerate_den
                                     / (int64_t)session.framerate_num;

                    session.encoder->attach_buffer(session.last_decoded_frame->data(),
                                                   session.last_decoded_frame->size(),
                                                   session.next_pts);
                    while(true)
                    {
                        auto enc_state = session.encoder->encode();
                        if(enc_state != R_CODEC_STATE_HAS_OUTPUT) break;
                        auto pi = session.encoder->get();
                        if(dup_ts >= start_ms)
                        {
                            out_bt["frames"][out_frame_idx]["stream_id"] = to_string((int)R_STORAGE_MEDIA_TYPE_VIDEO);
                            out_bt["frames"][out_frame_idx]["ts"]        = to_string(dup_ts);
                            out_bt["frames"][out_frame_idx]["key"]       = string(pi.key ? "true" : "false");
                            out_bt["frames"][out_frame_idx]["data"]      = vector<uint8_t>(pi.data, pi.data + pi.size);
                            ++out_frame_idx;
                        }
                    }
                    ++session.next_pts;
                }

                // Encode the current frame at next_pts.
                session.encoder->attach_buffer(decoded->data(), decoded->size(), session.next_pts);
                session.last_decoded_frame = decoded;

                while(true)
                {
                    auto enc_state = session.encoder->encode();
                    if(enc_state != R_CODEC_STATE_HAS_OUTPUT)
                        break;

                    auto pi = session.encoder->get();
                    if(ts >= start_ms)
                    {
                        out_bt["frames"][out_frame_idx]["stream_id"] = to_string((int)R_STORAGE_MEDIA_TYPE_VIDEO);
                        out_bt["frames"][out_frame_idx]["ts"]        = to_string(ts);
                        out_bt["frames"][out_frame_idx]["key"]       = string(pi.key ? "true" : "false");
                        out_bt["frames"][out_frame_idx]["data"]      = vector<uint8_t>(pi.data, pi.data + pi.size);
                        ++out_frame_idx;
                    }
                }
                ++session.next_pts;
            }
            else if(sid == R_STORAGE_MEDIA_TYPE_AUDIO && session.audio_initialized && ts >= start_ms)
            {
                if(session.audio_first_ts == -1)
                    session.audio_first_ts = ts;

                session.audio_decoder->attach_buffer(frame_data.data(), frame_data.size());
                auto dec_state = session.audio_decoder->decode();

                if(dec_state == R_CODEC_STATE_HAS_OUTPUT || dec_state == R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                {
                    // Decode to FLTP; channels are stored sequentially in the returned buffer.
                    auto decoded = session.audio_decoder->get(
                        AV_SAMPLE_FMT_FLTP, session.audio_sample_rate, session.audio_channels
                    );

                    int nb_samples = (int)(decoded->size() / (session.audio_channels * sizeof(float)));
                    const float* src = reinterpret_cast<const float*>(decoded->data());
                    for(int ch = 0; ch < session.audio_channels; ++ch)
                    {
                        auto& chbuf = session.audio_channel_buffers[ch];
                        chbuf.insert(chbuf.end(), src + ch * nb_samples, src + ch * nb_samples + nb_samples);
                    }

                    // Encode one AAC frame for every frame_size samples accumulated.
                    int frame_size = session.audio_encoder->get_frame_size();
                    while((int)session.audio_channel_buffers[0].size() >= frame_size)
                    {
                        // Pack FLTP buffer: [ch0 frame_size floats, ch1 frame_size floats, ...]
                        vector<uint8_t> enc_buf(session.audio_channels * frame_size * sizeof(float));
                        float* dst = reinterpret_cast<float*>(enc_buf.data());
                        for(int ch = 0; ch < session.audio_channels; ++ch)
                        {
                            auto& chbuf = session.audio_channel_buffers[ch];
                            memcpy(dst + ch * frame_size, chbuf.data(), frame_size * sizeof(float));
                            chbuf.erase(chbuf.begin(), chbuf.begin() + frame_size);
                        }

                        int64_t frame_sample_pos = session.audio_pts;
                        session.audio_encoder->attach_buffer(enc_buf.data(), enc_buf.size(), session.audio_pts);
                        session.audio_pts += frame_size;

                        auto enc_state = session.audio_encoder->encode();
                        if(enc_state == R_CODEC_STATE_HAS_OUTPUT)
                        {
                            // Compute a sample-accurate absolute timestamp so that each encoded
                            // AAC frame gets a unique PTS even when multiple frames are produced
                            // from a single decoded input packet.
                            int64_t audio_out_ts = session.audio_first_ts
                                                   + frame_sample_pos * 1000LL / session.audio_sample_rate;
                            auto pi = session.audio_encoder->get();
                            out_bt["frames"][out_frame_idx]["stream_id"] = to_string((int)R_STORAGE_MEDIA_TYPE_AUDIO);
                            out_bt["frames"][out_frame_idx]["ts"]        = to_string(audio_out_ts);
                            out_bt["frames"][out_frame_idx]["key"]       = string("true");
                            out_bt["frames"][out_frame_idx]["data"]      = vector<uint8_t>(pi.data, pi.data + pi.size);
                            ++out_frame_idx;
                        }
                    }
                }
            }
        }

        // Some encoders (e.g. h264_mf on Windows) put SPS/PPS inline in IDR frames rather
        // than in global extradata. Detect this by checking AVCC num_sps==0 (ed[5] & 0x1F).
        // In that case, pull SPS/PPS from the first key frame and rebuild codec params.
        {
            auto ed = session.encoder->get_extradata();
            bool avcc_no_sps = (ed.size() >= 7 && (ed[5] & 0x1F) == 0);
            bool needs_rebuild = session.output_video_codec_params.empty() || avcc_no_sps;
            if(needs_rebuild)
            {
                r_nullable<vector<uint8_t>> sps_nal;
                r_nullable<vector<uint8_t>> pps_nal;

                for(size_t fi = 0; fi < out_frame_idx; ++fi)
                {
                    if(!out_bt["frames"].has_index(fi)) continue;
                    if(out_bt["frames"][fi]["key"].get_string() != "true") continue;

                    auto& fdata = out_bt["frames"][fi]["data"].get_blob();
                    auto sps = _extract_annexb_nalu(fdata.data(), fdata.size(), 7); // SPS = type 7
                    auto pps = _extract_annexb_nalu(fdata.data(), fdata.size(), 8); // PPS = type 8

                    if(!sps.empty() && !pps.empty())
                    {
                        sps_nal = sps;
                        pps_nal = pps;
                        R_LOG_INFO("[TRANSCODE] Extracted SPS(%zu bytes) and PPS(%zu bytes) from key frame %zu",
                                   sps.size(), pps.size(), fi);
                        break;
                    }
                }

                if(!sps_nal.is_null() && !pps_nal.is_null())
                {
                    try
                    {
                        auto new_ed = r_pipeline::make_h264_avcc_extradata(sps_nal, pps_nal);
                        float fr = (float)session.framerate_num / (float)session.framerate_den;
                        session.output_video_codec_params = r_pipeline::build_video_codec_params(
                            session.output_codec_name, new_ed, fr
                        );
                        out_bt["video_codec_parameters"] = session.output_video_codec_params;
                        R_LOG_INFO("[TRANSCODE] Rebuilt codec params from inline SPS/PPS: %s",
                                   session.output_video_codec_params.c_str());
                    }
                    catch(const std::exception& ex)
                    {
                        R_LOG_WARNING("[TRANSCODE] Failed to rebuild codec params from inline SPS/PPS: %s", ex.what());
                    }
                }
                else if(session.output_video_codec_params.empty())
                {
                    // Last resort: try with whatever extradata the encoder has.
                    float fr = (float)session.framerate_num / (float)session.framerate_den;
                    try
                    {
                        session.output_video_codec_params = r_pipeline::build_video_codec_params(
                            session.output_codec_name, ed, fr
                        );
                        out_bt["video_codec_parameters"] = session.output_video_codec_params;
                    }
                    catch(const std::exception& ex)
                    {
                        R_LOG_WARNING("[TRANSCODE] Could not build codec params after encoding: %s", ex.what());
                    }
                }
            }
        }

        auto serialized = r_blob_tree::serialize(out_bt, 0);

        auto t_done = steady_clock::now();
        auto ms = [](steady_clock::duration d) { return (long long)duration_cast<milliseconds>(d).count(); };
        R_LOG_INFO("[TRANSCODE] %s %s to %s: %lld ms (waited %lld ms for the session, read %zu bytes of video in %lld ms, decoded and encoded in %lld ms)",
                   args["camera_id"].c_str(), args["start_time"].c_str(), args["end_time"].c_str(),
                   ms(t_done - t_start), ms(t_locked - t_start), qr_buffer.size(), ms(t_read - t_locked), ms(t_done - t_read));

        r_server_response response;
        response.set_content_type("application/vnd.revere.blobtree.v1");
        response.set_body(serialized.size(), serialized.data());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to transcode video."));
}

// Transcode a short window of recorded video into a fragmented MP4 the browser
// can append directly via Media Source Extensions. Stateless and self-contained
// (no transcode handle): each call decodes the stored GOP covering [start,end],
// re-encodes the frames inside the window through a fresh encoder — so the output
// always begins with a leading IDR and is independently decodable — and muxes the
// result to an in-memory fragmented MP4.
//
// pts_base places the window on the client's MSE timeline: output frame PTS is
// offset by the frame distance between pts_base and the window start, re-anchored
// every call so intra-window framerate drift can't accumulate across the timeline.
// With contiguous windows sharing one pts_base, the fragments tile seamlessly and
// the client appends them back-to-back with no timestampOffset juggling.
//
// Audio is AAC, see _fmp4_aac_audio.

// Audio for the fMP4 window endpoints: decodes the stored audio (AAC or G.711
// mulaw/alaw) and re-encodes it as AAC into the window's muxer. Output is
// 0-based within the window and aligned to base_ms (via offset_samples) so it
// stays in sync with the video. Shared by the transcode path (which always
// uses it) and the passthrough path (which uses it only for G.711 — AAC is
// remuxed straight through there).
struct _fmp4_aac_audio
{
    unique_ptr<r_audio_decoder> decoder;
    unique_ptr<r_audio_encoder> encoder;
    vector<vector<float>>       channel_buffers;
    int     channels       = 0;
    int     sample_rate    = 0;
    int64_t pts            = 0;    // running sample position fed to the encoder
    int64_t first_ts       = -1;   // ts of the first in-window audio packet
    int64_t offset_samples = 0;    // (first_ts - base_ms) in samples

    bool ready() const { return (bool)encoder; }

    // Adds the AAC audio stream to `muxer` (call before muxer.open()). Returns
    // false when the stored audio can't be described (no sample rate).
    bool init(r_blob_tree& bt, r_muxer& muxer)
    {
        auto a_name   = bt["audio_codec_name"].get_string();
        auto a_params = bt["audio_codec_parameters"].get_string();
        auto a_id     = r_av::encoding_to_av_codec_id(a_name);

        r_nullable<int> a_rate, a_chans;
        for(auto& part : r_string_utils::split(a_params, ","))
        {
            auto kv = r_string_utils::split(part, "=");
            if(kv.size() != 2) continue;
            if(r_string_utils::strip(kv[0]) == "sc_audio_rate")     a_rate.set_value(r_string_utils::s_to_int(kv[1]));
            if(r_string_utils::strip(kv[0]) == "sc_audio_channels") a_chans.set_value(r_string_utils::s_to_int(kv[1]));
        }
        if(a_chans.is_null()) a_chans.set_value(1);
        if(a_rate.is_null() && (a_id == AV_CODEC_ID_PCM_MULAW || a_id == AV_CODEC_ID_PCM_ALAW))
            a_rate.set_value(8000);
        if(a_rate.is_null())
            return false;

        channels    = a_chans.value();
        sample_rate = a_rate.value();

        decoder = make_unique<r_audio_decoder>(a_id);
        auto a_asc = r_pipeline::get_audio_codec_extradata(a_params);
        if(!a_asc.empty()) decoder->set_extradata(a_asc);
        if(a_id == AV_CODEC_ID_PCM_MULAW || a_id == AV_CODEC_ID_PCM_ALAW)
            decoder->set_pcm_params(sample_rate, channels);

        encoder = make_unique<r_audio_encoder>(
            AV_CODEC_ID_AAC, (uint32_t)(64000 * channels),
            sample_rate, channels, AV_SAMPLE_FMT_FLTP);
        channel_buffers.assign(channels, {});

        muxer.add_audio_stream(AV_CODEC_ID_AAC, (uint8_t)channels, (uint32_t)sample_rate);
        muxer.set_audio_extradata(encoder->get_extradata());
        return true;
    }

    // One stored audio packet with real timestamp `ts` (epoch ms). `base_ms` is
    // the window's PTS-0 instant; the caller has already dropped packets before it.
    void feed(r_muxer& muxer, int64_t ts, int64_t base_ms, const uint8_t* data, size_t size)
    {
        if(first_ts == -1)
        {
            first_ts = ts;
            offset_samples = ((ts - base_ms) * (int64_t)sample_rate + 500LL) / 1000LL;
        }

        decoder->attach_buffer(data, size);
        auto a_dec = decoder->decode();
        if(a_dec != R_CODEC_STATE_HAS_OUTPUT && a_dec != R_CODEC_STATE_AGAIN_HAS_OUTPUT)
            return;

        auto adecoded = decoder->get(AV_SAMPLE_FMT_FLTP, sample_rate, channels);
        int nb = (int)(adecoded->size() / (channels * sizeof(float)));
        const float* src = reinterpret_cast<const float*>(adecoded->data());
        for(int ch = 0; ch < channels; ++ch)
            channel_buffers[ch].insert(channel_buffers[ch].end(), src + ch*nb, src + ch*nb + nb);

        // Encode a full AAC frame for every frame_size samples accumulated.
        int fsz = encoder->get_frame_size();
        while((int)channel_buffers[0].size() >= fsz)
        {
            vector<uint8_t> ebuf(channels * fsz * sizeof(float));
            float* dst = reinterpret_cast<float*>(ebuf.data());
            for(int ch = 0; ch < channels; ++ch)
            {
                memcpy(dst + ch*fsz, channel_buffers[ch].data(), fsz*sizeof(float));
                channel_buffers[ch].erase(channel_buffers[ch].begin(), channel_buffers[ch].begin() + fsz);
            }
            encoder->attach_buffer(ebuf.data(), ebuf.size(), pts);
            pts += fsz;
            if(encoder->encode() == R_CODEC_STATE_HAS_OUTPUT)
            {
                auto api = encoder->get();
                muxer.write_audio_frame(api.data, api.size, offset_samples + api.pts, AVRational{1, sample_rate});
            }
        }
    }

    // Drain the encoder's buffered frame(s) so the window's audio isn't clipped
    // at its tail.
    void flush(r_muxer& muxer)
    {
        auto a_flush = encoder->flush();
        while(a_flush == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto api = encoder->get();
            muxer.write_audio_frame(api.data, api.size, offset_samples + api.pts, AVRational{1, sample_rate});
            a_flush = encoder->flush();
        }
    }
};

static bool _fmp4_has_audio(r_blob_tree& bt)
{
    return bt.has_key("has_audio") && bt["has_audio"].get_string() == "true"
           && bt.has_key("audio_codec_name") && bt.has_key("audio_codec_parameters");
}

r_http::r_server_response r_ws::_get_transcode_fmp4(r_utils::r_socket_base& conn, const r_http::r_server_request& request)
{
    // passthrough=1 selects the remux-only mode (separate budget, no codecs).
    {
        const auto& pre_args = request.get_uri().get_get_args();
        auto pit = pre_args.find("passthrough");
        if(pit != pre_args.end() && (pit->second == "1" || pit->second == "true"))
            return _get_passthrough_fmp4(conn, pre_args);
    }

    // Concurrency ceiling: reserve a slot, or 503 so the client backs off and
    // retries. The guard releases the slot on every exit path below.
    if(_fmp4_active.fetch_add(1) >= MAX_FMP4_TRANSCODES)
    {
        _fmp4_active.fetch_sub(1);
        r_server_response busy(r_http::response_service_unavailable, "text/plain");
        busy.add_additional_header("Retry-After", "1");
        busy.set_body(string("Too many concurrent transcodes."));
        return busy;
    }
    struct slot_guard { std::atomic<int>& c; ~slot_guard() { c.fetch_sub(1); } } guard{_fmp4_active};

    try
    {
        auto args = request.get_uri().get_get_args();

        for(auto k : {"camera_id", "start_time", "end_time", "width", "height",
                      "bitrate", "framerate_num", "framerate_den"})
            if(args.find(k) == args.end())
                R_THROW(("Missing %s.", k));

        auto camera_id = args["camera_id"];
        auto start_tp  = r_time_utils::iso_8601_to_tp(args["start_time"]);
        auto end_tp    = r_time_utils::iso_8601_to_tp(args["end_time"]);
        int64_t start_ms = chrono::duration_cast<chrono::milliseconds>(start_tp.time_since_epoch()).count();

        uint16_t output_width  = r_string_utils::s_to_uint16(args["width"]);
        uint16_t output_height = r_string_utils::s_to_uint16(args["height"]);
        uint32_t bitrate       = r_string_utils::s_to_uint32(args["bitrate"]);
        uint32_t framerate_num = r_string_utils::s_to_uint32(args["framerate_num"]);
        uint32_t framerate_den = r_string_utils::s_to_uint32(args["framerate_den"]);

        string output_codec = (args.find("codec") != args.end()) ? r_string_utils::to_lower(args["codec"]) : "h264";
        if(output_codec != "h264" && output_codec != "h265" && output_codec != "hevc")
            R_THROW(("Unsupported output codec: %s", output_codec.c_str()));

        AVRational framerate{(int)framerate_num, (int)framerate_den};

        // Each window is emitted 0-based (PTS starts at 0). The client positions it
        // on the MSE timeline with SourceBuffer.timestampOffset, so we don't depend
        // on how the muxer writes baseMediaDecodeTime for an independent fragment.
        auto qr_buffer = query_get_video(_top_dir, _devices, camera_id, start_tp, end_tp);

        uint32_t version = 0;
        auto bt = r_blob_tree::deserialize(qr_buffer.data(), qr_buffer.size(), version);

        if(!bt.has_key("video_codec_name") || !bt.has_key("video_codec_parameters"))
            R_THROW(("Blob tree missing video codec info."));

        auto in_codec_name   = bt["video_codec_name"].get_string();
        auto in_codec_params = bt["video_codec_parameters"].get_string();
        auto in_codec_id     = r_av::encoding_to_av_codec_id(in_codec_name);
        auto in_extradata    = r_pipeline::get_video_codec_extradata(in_codec_name, in_codec_params);

        // Software decode/encode on purpose: this on-demand path spins up a fresh
        // codec per short window, and churning hardware codec sessions (while the
        // recorder is also using them) intermittently stalls. 720p software is
        // plenty fast for playback windows.
        r_video_decoder decoder(in_codec_id);
        if(!in_extradata.empty())
            decoder.set_extradata(in_extradata);

        bool is_h265      = (output_codec == "h265" || output_codec == "hevc");
        auto out_codec_id = r_av::encoding_to_av_codec_id(output_codec);
        int profile       = is_h265 ? AV_PROFILE_HEVC_MAIN : AV_PROFILE_H264_MAIN;
        int level         = is_h265 ? 120 : 41;

        r_video_encoder encoder(
            out_codec_id, bitrate, output_width, output_height, framerate,
            AV_PIX_FMT_YUV420P, 0, (uint16_t)framerate_num, profile, level,
            "", "", r_hw_accel::none
        );

        r_muxer muxer("", /*output_to_buffer=*/true, "mp4");
        muxer.enable_fragmented_mp4();
        muxer.add_video_stream(framerate, out_codec_id, output_width, output_height, profile, level);
        muxer.set_video_extradata(encoder.get_extradata());

        // --- audio (AAC), only if the source stream carries it; 0-based per
        // window and aligned to start_ms so it stays in sync with the video.
        _fmp4_aac_audio audio;
        if(_fmp4_has_audio(bt))
            audio.init(bt, muxer);

        muxer.open();

        int64_t last_pts = -1;   // last emitted output PTS (encoder timebase units)

        if(bt.has_key("frames"))
        {
            size_t n_frames = bt["frames"].size();
            for(size_t fi = 0; fi < n_frames; ++fi)
            {
                // Cooperative shutdown: don't make teardown wait for the whole
                // window to transcode.
                if(_stopping())
                    R_THROW(("fMP4 transcode aborted: server shutting down."));
                if(conn.peer_closed())
                    R_THROW(("fMP4 transcode aborted: client disconnected."));

                if(!bt["frames"].has_index(fi)) continue;

                auto sid        = bt["frames"][fi]["stream_id"].get_value<int>();
                auto ts         = bt["frames"][fi]["ts"].get_value<int64_t>();
                auto frame_data = bt["frames"][fi]["data"].get_blob();

                if(sid == R_STORAGE_MEDIA_TYPE_VIDEO)
                {
                    // Decode every frame (frames before start_ms are pre-roll that prime
                    // the decoder's reference state) but only encode/emit those inside the
                    // requested window.
                    decoder.attach_buffer(frame_data.data(), frame_data.size());
                    auto dec_state = decoder.decode();
                    if(dec_state != R_CODEC_STATE_HAS_OUTPUT && dec_state != R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                        continue;

                    auto decoded = decoder.get(AV_PIX_FMT_YUV420P, output_width, output_height, 1);

                    if(ts < start_ms)
                        continue;

                    // Derive the output PTS from the real frame timestamp (0-based within
                    // the window, in the encoder's framerate timebase) rather than a frame
                    // counter. A frame counter assumes the source runs at exactly
                    // framerate_num/framerate_den; any other rate (e.g. a 15fps camera with
                    // a 30fps target) would play back at the wrong speed and make the window's
                    // duration not match its 3s slot, breaking the client's timestampOffset
                    // tiling. last_pts guards against a source faster than the target.
                    int64_t window_pts = ((ts - start_ms) * (int64_t)framerate_num + 500LL * (int64_t)framerate_den)
                                         / (1000LL * (int64_t)framerate_den);
                    if(window_pts <= last_pts)
                        continue;
                    last_pts = window_pts;

                    encoder.attach_buffer(decoded->data(), decoded->size(), window_pts);

                    while(true)
                    {
                        auto enc_state = encoder.encode();
                        if(enc_state != R_CODEC_STATE_HAS_OUTPUT) break;
                        auto pi = encoder.get();
                        // libx264 lookahead can make the first packet's DTS negative; clamp
                        // so av_interleaved_write_frame doesn't reject it.
                        int64_t out_dts = max((int64_t)0, pi.dts);
                        muxer.write_video_frame(pi.data, pi.size, pi.pts, out_dts, pi.time_base, pi.key);
                    }
                }
                else if(sid == R_STORAGE_MEDIA_TYPE_AUDIO && audio.ready() && ts >= start_ms)
                {
                    audio.feed(muxer, ts, start_ms, frame_data.data(), frame_data.size());
                }
            }
        }

        // Flush the encoder so the window's trailing frames land in this fragment.
        auto flush_state = encoder.flush();
        while(flush_state == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto pi = encoder.get();
            int64_t out_dts = max((int64_t)0, pi.dts);
            muxer.write_video_frame(pi.data, pi.size, pi.pts, out_dts, pi.time_base, pi.key);
            flush_state = encoder.flush();
        }

        if(audio.ready())
            audio.flush(muxer);

        muxer.finalize();

        // Tell the client the exact MSE codec string so it can create a matching
        // SourceBuffer (it can't know audio presence up front). h264 Main@4.1 here.
        string mse_codecs = is_h265 ? "hvc1.1.6.L120.90" : "avc1.4d4029";
        if(audio.ready()) mse_codecs += ",mp4a.40.2";

        r_server_response response;
        response.set_content_type("video/mp4");
        response.add_additional_header("X-Revere-Codecs", mse_codecs);
        response.set_body(muxer.buffer_size(), muxer.buffer());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to transcode fmp4."));
}

// MSE codec strings derived from the stored parameter sets. `sps` is in the
// get_h264_sps/get_h265_sps form: [00 00 00 01][NAL header][payload...].
static string _h264_mse_codec_string(const vector<uint8_t>& sps)
{
    // avc1.PPCCLL = profile_idc, constraint_set flags, level_idc, straight
    // from the first three SPS payload bytes.
    if(sps.size() < 8)
        return "avc1.4d4029";
    return r_string_utils::format("avc1.%02x%02x%02x", sps[5], sps[6], sps[7]);
}

static string _h265_mse_codec_string(uint8_t profile_idc, uint8_t level_idc)
{
    // hvc1.<profile>.<compat flags, bit-reversed hex>.<tier><level>.<constraints>
    // Compatibility flags: the profile's own bit, plus Main for Main10 streams
    // (Main10 decoders accept Main). B0 = progressive + frame-only, which is
    // what IP cameras emit. Tier is always Main.
    uint32_t compat = (profile_idc < 32) ? (1u << profile_idc) : 1u;
    if(profile_idc == 1) compat |= (1u << 2);
    return r_string_utils::format("hvc1.%u.%X.L%u.B0", (unsigned)profile_idc, compat, (unsigned)level_idc);
}

// Remux-only sibling of _get_transcode_fmp4 (selected with passthrough=1). The
// stored H.264/H.265 access units are written straight into a fragmented MP4
// with their real timestamps; no decoder or encoder is involved, so a request
// costs a storage read and a mux. This is what the Live-tab tiles use.
//
// Because frames are not re-encoded, a window can only start on a stored key
// frame, and a fragment must not end mid-GOP (the next request would have to
// re-send the GOP head). So the window is GOP-aligned, stateless, and gapless:
//
//   * emission starts at the key frame at or before start_time (the storage
//     query already backs up to it) — header X-Revere-First-Ts gives its real
//     epoch-ms, and that frame is PTS 0;
//   * emission stops *before* the last key frame inside [start,end) — header
//     X-Revere-Next-Start gives that key frame's epoch-ms. The client asks for
//     the next window starting exactly there, which lands on that key frame
//     with no pre-roll and no overlap;
//   * if the window holds only one key frame (not a whole GOP yet), the body is
//     empty and X-Revere-Next-Start repeats the window start: the client polls
//     again once more footage has been committed.
//
// Audio: AAC is passed through (X-Revere-Codecs gains ",mp4a.40.2"); G.711
// (mulaw/alaw) can't go in an MSE-playable MP4 as-is, so only the audio is
// transcoded to AAC (cheap; the video stays a pure remux). `audio=0` drops
// audio entirely — the muted Live-tab tiles use it. Only camera_id/start_time/
// end_time are required; width/height are optional fallbacks when the SPS
// can't be parsed.
//
// `through_end=1` (seeked playback at the end of a recorded segment): emit
// every frame in the window, including the final partial GOP, so the last
// seconds before a recording gap aren't lost; X-Revere-Next-Start is then one
// frame period past the last emitted frame.
//
// X-Revere-Frame-Duration-Ms carries the mean real frame period of the emitted
// video frames (measured from the stored timestamps, not the SPS, which cameras
// routinely get wrong), for the client's frame step.
r_http::r_server_response r_ws::_get_passthrough_fmp4(r_utils::r_socket_base& conn, std::map<std::string, std::string> args)
{
    if(_fmp4_passthrough_active.fetch_add(1) >= MAX_FMP4_PASSTHROUGH)
    {
        _fmp4_passthrough_active.fetch_sub(1);
        r_server_response busy(r_http::response_service_unavailable, "text/plain");
        busy.add_additional_header("Retry-After", "1");
        busy.set_body(string("Too many concurrent passthrough streams."));
        return busy;
    }
    struct slot_guard { std::atomic<int>& c; ~slot_guard() { c.fetch_sub(1); } } guard{_fmp4_passthrough_active};

    try
    {
        for(auto k : {"camera_id", "start_time", "end_time"})
            if(args.find(k) == args.end())
                R_THROW(("Missing %s.", k));

        auto camera_id = args["camera_id"];
        auto start_tp  = r_time_utils::iso_8601_to_tp(args["start_time"]);
        auto end_tp    = r_time_utils::iso_8601_to_tp(args["end_time"]);
        int64_t start_ms = r_time_utils::tp_to_epoch_millis(start_tp);

        auto flag = [&](const char* k) {
            auto it = args.find(k);
            return it != args.end() && (it->second == "1" || it->second == "true");
        };
        bool want_audio  = !(args.find("audio") != args.end()
                             && (args["audio"] == "0" || args["audio"] == "false"));
        bool through_end = flag("through_end");

        auto qr_buffer = query_get_video(_top_dir, _devices, camera_id, start_tp, end_tp);

        uint32_t version = 0;
        auto bt = r_blob_tree::deserialize(qr_buffer.data(), qr_buffer.size(), version);

        if(!bt.has_key("video_codec_name") || !bt.has_key("video_codec_parameters"))
            R_THROW(("Blob tree missing video codec info."));

        auto codec_name   = bt["video_codec_name"].get_string();
        auto codec_params = bt["video_codec_parameters"].get_string();
        auto codec_id     = r_av::encoding_to_av_codec_id(codec_name);
        bool is_h265      = (codec_id == AV_CODEC_ID_HEVC);

        if(codec_id != AV_CODEC_ID_H264 && codec_id != AV_CODEC_ID_HEVC)
            R_THROW(("Passthrough fmp4 supports only H.264/H.265 (got %s).", codec_name.c_str()));

        // --- pass 1: index the video frames so the GOP boundaries are known
        // before anything is muxed.
        struct vf { size_t idx; int64_t ts; bool key; };
        vector<vf> vframes;
        size_t n_frames = bt.has_key("frames") ? bt["frames"].size() : 0;
        for(size_t fi = 0; fi < n_frames; ++fi)
        {
            if(!bt["frames"].has_index(fi)) continue;
            if(bt["frames"][fi]["stream_id"].get_value<int>() != R_STORAGE_MEDIA_TYPE_VIDEO) continue;
            vframes.push_back({fi,
                               bt["frames"][fi]["ts"].get_value<int64_t>(),
                               bt["frames"][fi].has_key("key") && bt["frames"][fi]["key"].get_string() == "true"});
        }

        // The last key frame at or before start_ms (the query backs up to one, so
        // this is normally [0]; if start_ms itself is a key frame — a window that
        // begins at a recorded segment's first frame — prefer it over an earlier
        // one the storage query may have backed up to across the gap), falling
        // back to the first key frame; then the last key frame after it, which
        // is where this window stops.
        size_t first_i = vframes.size();
        for(size_t i = 0; i < vframes.size(); ++i)
            if(vframes[i].key && (first_i == vframes.size() || vframes[i].ts <= start_ms))
                first_i = i;
        size_t cut_i = vframes.size();
        if(!through_end)
            for(size_t i = vframes.size(); i > first_i + 1 && cut_i == vframes.size(); --i)
                if(vframes[i-1].key) cut_i = i - 1;

        if(first_i >= vframes.size() || (!through_end && cut_i == vframes.size()))
        {
            // Not a whole GOP in the window yet (or, with through_end, no frames
            // at all): nothing to emit, come back later.
            r_server_response empty;
            empty.set_content_type("video/mp4");
            empty.add_additional_header("X-Revere-Next-Start", r_string_utils::int64_to_s(start_ms));
            return empty;
        }

        int64_t first_ts = vframes[first_i].ts;

        // --- stream description from the stored parameter sets.
        r_nullable<float> fr = r_pipeline::get_video_codec_framerate(codec_params);

        // Mean real frame period of the emitted frames [first_i, cut_i): n frames
        // span n-1 periods. One frame: fall back to the SPS rate, then 30 fps.
        size_t n_emit = cut_i - first_i;
        double frame_ms = (!fr.is_null() && fr.value() > 0.f) ? 1000.0 / fr.value() : 1000.0 / 30.0;
        if(n_emit >= 2)
        {
            int64_t span = vframes[cut_i-1].ts - first_ts;
            if(span > 0) frame_ms = (double)span / (double)(n_emit - 1);
        }

        // Normal: stop before the last key frame. through_end: everything, and
        // the next window starts one frame period after the last frame.
        int64_t cut_ts = through_end ? vframes[cut_i-1].ts + max((int64_t)1, (int64_t)llround(frame_ms))
                                     : vframes[cut_i].ts;

        if(fr.is_null())
            fr.set_value((float)(1000.0 / frame_ms));

        uint16_t width = 0, height = 0;
        int profile = 0, level = 0;
        string mse_codecs;
        // Annex-B extradata, as the export remux uses: libavformat's mov muxer
        // builds the avcC/hvcC itself from it and, seeing Annex-B extradata,
        // converts the Annex-B access units to length-prefixed samples.
        vector<uint8_t> extradata;
        if(is_h265)
        {
            auto sps = r_pipeline::get_h265_sps(codec_params);
            if(!sps.is_null())
            {
                auto info = r_pipeline::parse_h265_sps(sps.value());
                width = info.width; height = info.height;
                profile = info.profile_idc; level = info.level_idc;
                mse_codecs = _h265_mse_codec_string(info.profile_idc, info.level_idc);
            }
            else mse_codecs = "hvc1.1.6.L120.B0";
            extradata = r_pipeline::get_video_codec_extradata(codec_name, codec_params);
        }
        else
        {
            auto sps = r_pipeline::get_h264_sps(codec_params);
            auto pps = r_pipeline::get_h264_pps(codec_params);
            if(sps.is_null())
            {
                // Stored codec params without sprop-parameter-sets: the camera
                // sends SPS/PPS inline in its IDR frames instead. Lift them from
                // the first key frame (in the start-code-prefixed form the
                // get_h264_* helpers return) so the init segment is still valid.
                auto& kf = bt["frames"][vframes[first_i].idx]["data"].get_blob();
                auto raw_sps = _extract_annexb_nalu(kf.data(), kf.size(), 7);
                auto raw_pps = _extract_annexb_nalu(kf.data(), kf.size(), 8);
                if(!raw_sps.empty())
                {
                    vector<uint8_t> v{0, 0, 0, 1}; v.insert(v.end(), raw_sps.begin(), raw_sps.end());
                    sps.set_value(v);
                }
                if(!raw_pps.empty())
                {
                    vector<uint8_t> v{0, 0, 0, 1}; v.insert(v.end(), raw_pps.begin(), raw_pps.end());
                    pps.set_value(v);
                }
            }
            if(!sps.is_null())
            {
                auto info = r_pipeline::parse_h264_sps(sps.value());
                width = info.width; height = info.height;
                profile = info.profile_idc; level = info.level_idc;
                mse_codecs = _h264_mse_codec_string(sps.value());
                extradata  = r_pipeline::make_h264_extradata(sps, pps);
            }
            else mse_codecs = "avc1.4d4029";
        }
        if(width == 0 || height == 0)
        {
            if(args.find("width") != args.end() && args.find("height") != args.end())
            {
                width  = r_string_utils::s_to_uint16(args["width"]);
                height = r_string_utils::s_to_uint16(args["height"]);
            }
            else R_THROW(("Passthrough fmp4: no SPS in stored codec parameters and no width/height given."));
        }

        // Without parameter sets the init segment can't describe the stream and
        // the mov muxer wouldn't convert the Annex-B samples; let the client fall
        // back to stills rather than hand it an unplayable fragment.
        if(extradata.empty())
            R_THROW(("Passthrough fmp4: no SPS/PPS available for %s stream.", codec_name.c_str()));

        r_muxer muxer("", /*output_to_buffer=*/true, "mp4");
        muxer.enable_fragmented_mp4();
        muxer.add_video_stream(av_d2q(fr.value(), 10000), codec_id, width, height, profile, level);
        muxer.set_video_extradata(extradata);

        // --- audio: AAC straight through; G.711 transcoded to AAC; anything
        // else (or audio=0) dropped.
        bool audio_passthrough = false;
        _fmp4_aac_audio audio_aac;
        if(want_audio && _fmp4_has_audio(bt))
        {
            auto a_name   = bt["audio_codec_name"].get_string();
            auto a_params = bt["audio_codec_parameters"].get_string();
            auto a_id     = r_av::encoding_to_av_codec_id(a_name);
            if(a_id == AV_CODEC_ID_PCM_MULAW || a_id == AV_CODEC_ID_PCM_ALAW)
            {
                audio_aac.init(bt, muxer);
            }
            else if(a_id == AV_CODEC_ID_AAC)
            {
                r_nullable<int> a_rate, a_chans;
                for(auto& part : r_string_utils::split(a_params, ","))
                {
                    auto kv = r_string_utils::split(part, "=");
                    if(kv.size() != 2) continue;
                    if(r_string_utils::strip(kv[0]) == "sc_audio_rate")     a_rate.set_value(r_string_utils::s_to_int(kv[1]));
                    if(r_string_utils::strip(kv[0]) == "sc_audio_channels") a_chans.set_value(r_string_utils::s_to_int(kv[1]));
                }
                if(a_chans.is_null()) a_chans.set_value(1);
                if(!a_rate.is_null())
                {
                    muxer.add_audio_stream(AV_CODEC_ID_AAC, (uint8_t)a_chans.value(), (uint32_t)a_rate.value());
                    auto asc = r_pipeline::get_audio_codec_extradata(a_params);
                    if(!asc.empty())
                        muxer.set_audio_extradata(asc);
                    audio_passthrough = true;
                }
            }
        }

        muxer.open();

        // --- pass 2: write every frame with ts in [first_ts, cut_ts) — selected
        // by timestamp, not index, since the query may list audio after video.
        // PTS is 0-based at first_ts in a 1/1000 timebase; DTS == PTS (IP camera
        // streams carry no B-frames, exactly as the export remux assumes).
        for(size_t fi = 0; fi < n_frames; ++fi)
        {
            if(_stopping())
                R_THROW(("fMP4 passthrough aborted: server shutting down."));
            if(conn.peer_closed())
                R_THROW(("fMP4 passthrough aborted: client disconnected."));

            if(!bt["frames"].has_index(fi)) continue;

            auto sid = bt["frames"][fi]["stream_id"].get_value<int>();
            auto ts  = bt["frames"][fi]["ts"].get_value<int64_t>();
            if(ts < first_ts || ts >= cut_ts) continue;

            auto frame = bt["frames"][fi]["data"].get_blob();

            if(sid == R_STORAGE_MEDIA_TYPE_VIDEO)
            {
                bool key = bt["frames"][fi].has_key("key") && bt["frames"][fi]["key"].get_string() == "true";
                muxer.write_video_frame(frame.data(), frame.size(), ts - first_ts, ts - first_ts, {1, 1000}, key);
            }
            else if(sid == R_STORAGE_MEDIA_TYPE_AUDIO && audio_passthrough)
            {
                muxer.write_audio_frame(frame.data(), frame.size(), ts - first_ts, {1, 1000});
            }
            else if(sid == R_STORAGE_MEDIA_TYPE_AUDIO && audio_aac.ready())
            {
                audio_aac.feed(muxer, ts, first_ts, frame.data(), frame.size());
            }
        }

        if(audio_aac.ready())
            audio_aac.flush(muxer);

        muxer.finalize();

        if(audio_passthrough || audio_aac.ready()) mse_codecs += ",mp4a.40.2";

        r_server_response response;
        response.set_content_type("video/mp4");
        response.add_additional_header("X-Revere-Codecs", mse_codecs);
        response.add_additional_header("X-Revere-First-Ts", r_string_utils::int64_to_s(first_ts));
        response.add_additional_header("X-Revere-Next-Start", r_string_utils::int64_to_s(cut_ts));
        response.add_additional_header("X-Revere-Frame-Duration-Ms", r_string_utils::format("%.3f", frame_ms));
        response.set_body(muxer.buffer_size(), muxer.buffer());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to remux passthrough fmp4."));
}

r_http::r_server_response r_ws::_get_finalize_transcode_stream(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("handle") == args.end())
            R_THROW(("Missing handle."));

        auto handle = args["handle"];

        {
            lock_guard<mutex> lock(_sessions_mutex);
            auto it = _sessions.find(handle);
            if(it == _sessions.end())
                R_THROW(("Unknown transcode handle: %s", handle.c_str()));
            _sessions.erase(it);
        }

        r_server_response response;
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to finalize transcode stream."));
}

r_http::r_server_response r_ws::_get_transcode_export(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto exports_path = _top_dir + PATH_SLASH + "exports";
        if(!r_fs::file_exists(exports_path))
            r_fs::mkdir(exports_path);

        auto args = request.get_uri().get_get_args();

        if(args.find("camera_id") == args.end())
            R_THROW(("Missing camera_id."));
        if(args.find("start_time") == args.end())
            R_THROW(("Missing start_time."));
        if(args.find("end_time") == args.end())
            R_THROW(("Missing end_time."));
        if(args.find("file_name") == args.end())
            R_THROW(("Missing file_name."));
        if(!is_safe_export_filename(args["file_name"]))
            R_THROW(("Invalid file_name."));
        if(args.find("width") == args.end())
            R_THROW(("Missing width."));
        if(args.find("height") == args.end())
            R_THROW(("Missing height."));
        if(args.find("bitrate") == args.end())
            R_THROW(("Missing bitrate."));
        if(args.find("framerate_num") == args.end())
            R_THROW(("Missing framerate_num."));
        if(args.find("framerate_den") == args.end())
            R_THROW(("Missing framerate_den."));

        export_job job;
        job.id = r_uuid::generate();
        job.type = export_type::transcode;
        job.camera_id = args["camera_id"];
        job.start_time = r_time_utils::iso_8601_to_tp(args["start_time"]);
        job.end_time = r_time_utils::iso_8601_to_tp(args["end_time"]);
        job.file_name = args["file_name"];
        job.exports_path = exports_path;
        job.width = r_string_utils::s_to_uint16(args["width"]);
        job.height = r_string_utils::s_to_uint16(args["height"]);
        job.bitrate = r_string_utils::s_to_uint32(args["bitrate"]);
        job.framerate_num = r_string_utils::s_to_uint32(args["framerate_num"]);
        job.framerate_den = r_string_utils::s_to_uint32(args["framerate_den"]);
        job.codec = (args.find("codec") != args.end()) ? r_string_utils::to_lower(args["codec"]) : "h264";

        {
            lock_guard<mutex> lock(_export_progress_mutex);

            size_t inflight = 0;
            for(const auto& kv : _export_progress)
                if(kv.second.percent_complete < 100) ++inflight;
            if(inflight >= MAX_INFLIGHT_EXPORTS)
                R_THROW(("Too many exports in progress; try again later."));

            _export_progress[job.id] = {0, {}};
        }

        _export_q.post(job);

        json j;
        j["id"] = job.id;

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to enqueue transcode export."));
}

void r_ws::_transcode_export(export_job& job)
{
    auto dot = job.file_name.rfind('.');
    auto ext = (dot != string::npos) ? job.file_name.substr(dot) : string{};
    auto output_path = job.exports_path + PATH_SLASH + job.file_name;
    auto temp_path = r_fs::temp_file_name(job.exports_path, "tmp_") + ext;

    r_muxer muxer(temp_path);
    muxer.enable_faststart();

    auto output_width = job.width;
    auto output_height = job.height;
    auto bitrate = job.bitrate;
    auto framerate_num = job.framerate_num;
    auto framerate_den = job.framerate_den;
    string output_codec = job.codec;
    AVRational framerate{(int)framerate_num, (int)framerate_den};

    auto qs = job.start_time;
    auto qe = job.end_time;

    bool initialized = false;
    bool has_audio = false;
    int64_t ts_first_frame = 0;
    int64_t encoder_frame_count = 0;

    unique_ptr<r_video_decoder> decoder;
    unique_ptr<r_video_encoder> encoder;

    bool done = false;
    while(!done)
    {
        if(!_export_running) break;

        auto rs = qs;
        auto re = rs;
        if(rs + chrono::minutes(5) >= qe)
        {
            re = qe;
            done = true;
        }
        else re = rs + chrono::minutes(5);

        auto qr_buffer = query_get_video(_top_dir, _devices, job.camera_id, rs, re);
        qs = re;

        {
            lock_guard<mutex> lock(_export_progress_mutex);
            auto total_ms = duration_cast<milliseconds>(job.end_time - job.start_time).count();
            auto done_ms  = duration_cast<milliseconds>(qs - job.start_time).count();
            auto pit = _export_progress.find(job.id);
            if(pit != _export_progress.end())
                pit->second.percent_complete = (total_ms > 0) ? (int)(done_ms * 100 / total_ms) : 100;
        }

        try
        {
            uint32_t version = 0;
            auto bt = r_blob_tree::deserialize(qr_buffer.data(), qr_buffer.size(), version);

            if(!initialized)
            {
                if(!bt.has_key("video_codec_name"))
                    R_THROW(("Blob tree missing video_codec_name."));
                if(!bt.has_key("video_codec_parameters"))
                    R_THROW(("Blob tree missing video_codec_parameters."));

                auto in_codec_name   = bt["video_codec_name"].get_string();
                auto in_codec_params = bt["video_codec_parameters"].get_string();

                has_audio = bt.has_key("has_audio") && (bt["has_audio"].get_string() == "true");

                auto in_codec_id  = r_av::encoding_to_av_codec_id(in_codec_name);
                auto in_extradata = r_pipeline::get_video_codec_extradata(in_codec_name, in_codec_params);

                decoder = make_unique<r_video_decoder>(in_codec_id, r_av::r_find_best_hw_accel(in_codec_id));
                if(!in_extradata.empty())
                    decoder->set_extradata(in_extradata);

                bool is_h265 = (output_codec == "h265" || output_codec == "hevc");
                auto out_codec_id = r_av::encoding_to_av_codec_id(output_codec);
                int profile = is_h265 ? AV_PROFILE_HEVC_MAIN : AV_PROFILE_H264_MAIN;
                int level   = is_h265 ? 120 : 41;

                encoder = make_unique<r_video_encoder>(
                    out_codec_id,
                    bitrate,
                    output_width,
                    output_height,
                    framerate,
                    AV_PIX_FMT_YUV420P,
                    0,
                    (uint16_t)framerate_num,
                    profile,
                    level,
                    "",
                    "",
                    r_find_best_hw_accel_encoder(out_codec_id)
                );

                // Use the encoder's own AVCC/HVCC extradata to configure the muxer.
                // profile and level were already computed above and used to create the
                // encoder, so they are the authoritative values for the mux header too.
                auto enc_extradata = encoder->get_extradata();
                muxer.add_video_stream(framerate, out_codec_id, output_width, output_height, profile, level);
                muxer.set_video_extradata(enc_extradata);

                if(has_audio)
                {
                    auto audio_codec_name   = bt["audio_codec_name"].get_string();
                    auto audio_codec_params = bt["audio_codec_parameters"].get_string();

                    r_nullable<int> audio_rate, audio_channels;
                    for(auto& part : r_string_utils::split(audio_codec_params, ","))
                    {
                        auto kv = r_string_utils::split(part, "=");
                        if(kv.size() != 2) continue;
                        if(r_string_utils::strip(kv[0]) == "sc_audio_rate")
                            audio_rate.set_value(r_string_utils::s_to_int(kv[1]));
                        if(r_string_utils::strip(kv[0]) == "sc_audio_channels")
                            audio_channels.set_value(r_string_utils::s_to_int(kv[1]));
                    }

                    auto audio_codec_id = r_av::encoding_to_av_codec_id(audio_codec_name);

                    if(audio_channels.is_null()) audio_channels.set_value(1);
                    if(audio_rate.is_null())
                    {
                        if(audio_codec_id == AV_CODEC_ID_PCM_MULAW || audio_codec_id == AV_CODEC_ID_PCM_ALAW)
                            audio_rate.set_value(8000);
                    }
                    if(audio_rate.is_null())
                        R_THROW(("Missing audio rate."));

                    muxer.add_audio_stream(audio_codec_id, (uint8_t)audio_channels.value(), (uint16_t)audio_rate.value());
                }

                muxer.open();
                initialized = true;
            }

            if(!bt.has_key("frames"))
                R_THROW(("Blob tree missing frames."));

            size_t n_frames = bt["frames"].size();
            for(size_t fi = 0; fi < n_frames; ++fi)
            {
                // Cooperative shutdown: this runs on the background export
                // thread (joined separately in stop()), so it polls the export
                // running flag rather than the web server.
                if(!_export_running.load(std::memory_order_acquire))
                    R_THROW(("Transcode export aborted: server shutting down."));

                if(!bt["frames"].has_index(fi)) continue;

                auto sid        = bt["frames"][fi]["stream_id"].get_value<int>();
                auto ts         = bt["frames"][fi]["ts"].get_value<int64_t>();
                auto frame_data = bt["frames"][fi]["data"].get_blob();

                if(ts_first_frame == 0)
                    ts_first_frame = ts;

                int64_t rel_ts = ts - ts_first_frame;

                if(sid == R_STORAGE_MEDIA_TYPE_VIDEO)
                {
                    decoder->attach_buffer(frame_data.data(), frame_data.size());
                    auto dec_state = decoder->decode();

                    if(dec_state != R_CODEC_STATE_HAS_OUTPUT && dec_state != R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                        continue;

                    auto decoded = decoder->get(AV_PIX_FMT_YUV420P, output_width, output_height, 1);

                    // Use a monotonic frame counter rather than rescaling from ms.
                    // av_rescale_q(33ms, {1,1000}, {1,30}) == 0 due to integer truncation,
                    // which produces duplicate/invalid PTS values.
                    encoder->attach_buffer(decoded->data(), decoded->size(), encoder_frame_count++);

                    while(true)
                    {
                        auto enc_state = encoder->encode();
                        if(enc_state != R_CODEC_STATE_HAS_OUTPUT) break;
                        auto pi = encoder->get();
                        // libx264 lookahead makes the first packet's DTS negative.
                        // Clamp to 0 so av_interleaved_write_frame doesn't reject it.
                        int64_t out_dts = max((int64_t)0, pi.dts);
                        muxer.write_video_frame(pi.data, pi.size, pi.pts, out_dts, pi.time_base, pi.key);
                    }
                }
                else if(sid == R_STORAGE_MEDIA_TYPE_AUDIO && has_audio)
                {
                    muxer.write_audio_frame(frame_data.data(), frame_data.size(), rel_ts, {1, 1000});
                }
            }
        }
        catch(const exception& ex)
        {
            R_LOG_WARNING("_transcode_export(): skipping chunk [%s -> %s]: %s",
                r_time_utils::tp_to_iso_8601(rs, false).c_str(),
                r_time_utils::tp_to_iso_8601(re, false).c_str(),
                ex.what()
            );
        }
    }

    if(initialized)
    {
        if(encoder)
        {
            auto flush_state = encoder->flush();
            while(flush_state == R_CODEC_STATE_HAS_OUTPUT)
            {
                auto pi = encoder->get();
                int64_t out_dts = max((int64_t)0, pi.dts);
                muxer.write_video_frame(pi.data, pi.size, pi.pts, out_dts, pi.time_base, pi.key);
                flush_state = encoder->flush();
            }
        }

        muxer.finalize();
        r_fs::atomic_rename_file(temp_path, output_path);
    }
}

r_http::r_server_response r_ws::_get_export_progress(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("id") == args.end())
            R_THROW(("Missing id."));

        auto id = args["id"];

        lock_guard<mutex> lock(_export_progress_mutex);
        auto it = _export_progress.find(id);
        if(it == _export_progress.end())
            R_STHROW(r_http_404_exception, ("Export not found: %s", id.c_str()));

        json j;
        j["id"] = id;
        j["percent_complete"] = it->second.percent_complete;
        j["failed"] = it->second.failed;

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const r_http_404_exception&)
    {
        throw;
    }
    catch(const exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to get export progress."));
}

r_http::r_server_response r_ws::_get_export_download(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();
        if(args.find("id") == args.end())
            R_THROW(("Missing id."));
        auto id = args["id"];

        std::string file_path;
        {
            lock_guard<mutex> lock(_export_progress_mutex);
            auto it = _export_progress.find(id);
            if(it == _export_progress.end())
                R_STHROW(r_http_404_exception, ("Export not found: %s", id.c_str()));
            if(it->second.percent_complete < 100)
                R_STHROW(r_http_500_exception, ("Export not yet complete: %s", id.c_str()));
            file_path = it->second.file_path;
        }

        if(!r_fs::file_exists(file_path))
            R_STHROW(r_http_404_exception, ("Export file missing on disk: %s", file_path.c_str()));

        // basename for Content-Disposition.
        auto slash = file_path.find_last_of("/\\");
        auto file_name = (slash == std::string::npos) ? file_path : file_path.substr(slash + 1);

        auto bytes = r_fs::read_file(file_path);

        r_server_response response;
        response.set_content_type("video/quicktime");
        response.add_additional_header(
            "Content-Disposition",
            string("attachment; filename=\"") + file_name + "\""
        );
        response.set_body(std::move(bytes));

        // Delete the file and forget the job once we've assembled the response.
        // If write_response then fails, the user can't retry — but they can just
        // re-run the export. Cleaner than letting orphaned files accumulate.
        try { r_fs::remove_file(file_path); } catch(...) {}
        {
            lock_guard<mutex> lock(_export_progress_mutex);
            _export_progress.erase(id);
        }

        return response;
    }
    catch(const r_http_404_exception&) { throw; }
    catch(const exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to download export."));
}

r_http::r_server_response r_ws::_get_camera_profiles(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("camera_id") == args.end())
            R_THROW(("Missing camera_id."));

        auto maybe_camera = _devices.get_camera_by_id(args["camera_id"]);
        if(maybe_camera.is_null())
            R_THROW(("Unknown camera id: %s", args["camera_id"].c_str()));
        auto camera = maybe_camera.value();

        if(camera.ipv4.is_null() || camera.xaddrs.is_null())
            R_THROW(("Camera is missing ipv4/xaddrs (not an ONVIF discovery)."));

        r_nullable<string> username, password;
        if(args.find("username") != args.end()) username.set_value(args["username"]);
        if(args.find("password") != args.end()) password.set_value(args["password"]);

        auto profiles = _agent.get_camera_profiles(camera.ipv4.value(), camera.xaddrs.value(), username, password, camera.onvif_port, camera.onvif_protocol);

        json j;
        j["profiles"] = json::array();
        for(auto& p : profiles)
        {
            // Only H.264/H.265 are recordable; hide anything else (e.g. MJPEG).
            if(p.encoding != "H264" && p.encoding != "H265")
                continue;
            j["profiles"].push_back({
                {"token",    p.token},
                {"encoding", p.encoding},
                {"width",    p.width},
                {"height",   p.height}
            });
        }

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to get camera profiles."));
}

r_http::r_server_response r_ws::_post_ptz(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    // Every answer is JSON. A failure is {error, code}: "bad_request", "unknown_camera", "no_ptz" (501: the
    // camera has no ONVIF PTZ service), "unreachable" or "camera_error" (502). error is a fixed sentence; the
    // camera's own words, which can name its address, go to the log only.
    auto failure = [](status_code status, const string& code, const string& error) {
        json j;
        j["error"] = error;
        j["code"] = code;
        r_server_response response(status);
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    };

    json req;
    try
    {
        req = json::parse(request.get_body_as_string());
    }
    catch(const std::exception&)
    {
        return failure(response_bad_request, "bad_request", "The request isn't JSON.");
    }

    if(!req.contains("camera_id") || !req["camera_id"].is_string())
        return failure(response_bad_request, "bad_request", "Missing camera_id.");
    auto camera_id = req["camera_id"].get<string>();

    auto maybe_camera = _devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        return failure(response_not_found, "unknown_camera", "No camera with that id.");
    auto camera = maybe_camera.value();

    // A camera added by its RTSP URL has no ONVIF address to send PTZ to.
    if(camera.ipv4.is_null() || camera.xaddrs.is_null())
        return failure(response_not_implemented, "no_ptz", "This camera wasn't found by ONVIF discovery, so it has no PTZ.");

    r_disco::r_ptz_command cmd;
    try
    {
        auto action = req.value("action", string("stop"));
        if(action == "move")
        {
            cmd.op = r_disco::r_ptz_command::op_t::move;
            cmd.pan = req.value("pan", 0.0f);
            cmd.tilt = req.value("tilt", 0.0f);
            cmd.zoom = req.value("zoom", 0.0f);
            cmd.timeout = req.value("timeout", string());
        }
        else if(action == "stop")
            cmd.op = r_disco::r_ptz_command::op_t::stop;
        else if(action == "goto_preset")
        {
            cmd.op = r_disco::r_ptz_command::op_t::goto_preset;
            cmd.preset_token = req.value("preset_token", string());
            if(cmd.preset_token.empty())
                return failure(response_bad_request, "bad_request", "goto_preset requires preset_token.");
        }
        else if(action == "list_presets")
            cmd.op = r_disco::r_ptz_command::op_t::list_presets;
        else
            return failure(response_bad_request, "bad_request", "Unknown PTZ action.");

        cmd.profile_token = req.value("profile_token", string());
    }
    catch(const std::exception&)
    {
        // A field of the wrong type (json throws on a mismatched value()).
        return failure(response_bad_request, "bad_request", "A PTZ field has the wrong type.");
    }

    try
    {
        // Creds come from the stored (decrypted) camera, like the events worker.
        auto ptz_result = _agent.ptz_control(
            camera.ipv4.value(), camera.xaddrs.value(),
            camera.rtsp_username, camera.rtsp_password, cmd,
            camera.onvif_port, camera.onvif_protocol
        );

        json j;
        j["status"] = "ok";
        j["presets"] = json::array();
        for(auto& p : ptz_result.presets)
            j["presets"].push_back({{"token", p.token}, {"name", p.name}});
        // Which axes the camera can actually drive, on list_presets only and only
        // when the camera said: a missing flag is unknown, so a client keeps the
        // control rather than read it as "can't".
        if(cmd.op == r_disco::r_ptz_command::op_t::list_presets)
        {
            if(!ptz_result.supports_pan_tilt.is_null())
                j["supports_pan_tilt"] = ptz_result.supports_pan_tilt.value();
            if(!ptz_result.supports_zoom.is_null())
                j["supports_zoom"] = ptz_result.supports_zoom.value();
        }

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const r_disco::r_ptz_error& ex)
    {
        R_LOG_ERROR("PTZ %s on camera %s failed (%s): %s", req.value("action", string("stop")).c_str(), camera_id.c_str(), ex.code().c_str(), ex.what());
        if(ex.code() == "no_ptz")
            return failure(response_not_implemented, "no_ptz", "This camera has no ONVIF PTZ service.");
        if(ex.code() == "unreachable")
            return failure(response_bad_gateway, "unreachable", "The camera's ONVIF service didn't answer.");
        return failure(response_bad_gateway, "camera_error", "The camera didn't carry out the PTZ command.");
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
        return failure(response_internal_server_error, "camera_error", "The camera didn't carry out the PTZ command.");
    }
}

// PTZ view's long edge: small enough to send several a second, never a full frame.
static constexpr uint16_t PTZ_VIEW_MIN_EDGE = 160;
static constexpr uint16_t PTZ_VIEW_MAX_EDGE = 1280;
static constexpr uint16_t PTZ_VIEW_DEFAULT_EDGE = 640;

// GET /ptz_view/frame?camera_id=&long_edge=&after=: the camera's newest decoded
// picture as a JPEG, for showing a PTZ move as it happens (r_frame_taps). The
// first ask starts decoding the camera's stream; it stops a few seconds after
// the last. after (epoch ms, X-Frame-Ts of the last picture) makes it answer 204
// while there's nothing newer, so a caller polling it sends each picture once.
// 404: no such camera, or it isn't recording; 503: too many cameras are
// being watched this way already.
r_http::r_server_response r_ws::_get_ptz_view_frame(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    auto args = request.get_uri().get_get_args();
    if(args.find("camera_id") == end(args) || args["camera_id"].empty())
        R_STHROW(r_http_400_exception, ("Missing camera_id."));
    auto camera_id = args["camera_id"];

    uint16_t long_edge = PTZ_VIEW_DEFAULT_EDGE;
    int64_t after = 0;
    try
    {
        if(args.find("long_edge") != end(args))
        {
            auto le = r_string_utils::s_to_uint16(args["long_edge"]);
            long_edge = le < PTZ_VIEW_MIN_EDGE ? PTZ_VIEW_MIN_EDGE : le > PTZ_VIEW_MAX_EDGE ? PTZ_VIEW_MAX_EDGE : le;
        }
        if(args.find("after") != end(args))
            after = r_string_utils::s_to_int64(args["after"]);
    }
    catch(const std::exception&)
    {
        R_STHROW(r_http_400_exception, ("long_edge and after must be numbers."));
    }

    if(_devices.get_camera_by_id(camera_id).is_null())
        R_STHROW(r_http_404_exception, ("No camera with that id."));

    auto result = _stream_keeper.ptz_view_frame(camera_id, long_edge, after);
    if(result.is_null())
        R_STHROW(r_http_404_exception, ("That camera isn't recording."));

    auto& r = result.value();
    if(r.status == r_frame_tap_status::too_many)
    {
        r_server_response response(response_service_unavailable);
        response.add_additional_header("Retry-After", "5");
        return response;
    }
    if(r.status == r_frame_tap_status::nothing_newer)
        return r_server_response(response_no_content);

    r_server_response response;
    response.set_content_type("image/jpeg");
    response.set_body(r.jpeg.size(), r.jpeg.data());
    response.add_additional_header("X-Frame-Ts", std::to_string(r.ts));
    return response;
}

r_http::r_server_response r_ws::_get_measure_camera(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("camera_id") == args.end())
            R_THROW(("Missing camera_id."));

        measure_job job;
        job.camera_id = args["camera_id"];
        if(args.find("username") != args.end()) job.username.set_value(args["username"]);
        if(args.find("password") != args.end()) job.password.set_value(args["password"]);
        if(args.find("profile_token") != args.end()) job.profile_token = args["profile_token"];

        auto id = r_uuid::generate();

        {
            lock_guard<mutex> lock(_measure_mutex);
            _measure_jobs[id] = job;
            _measure_results[id] = measure_result{};   // percent_complete = 0
        }

        _measure_q.post(id);

        json j;
        j["id"] = id;

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to start camera measurement."));
}

void r_ws::_measure_entry_point()
{
    while(true)
    {
        auto item = _measure_q.poll();
        if(item.is_null())
            break;

        auto job_id = item.value();

        try
        {
            _measure(job_id);
        }
        catch(const exception& ex)
        {
            R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
            lock_guard<mutex> lock(_measure_mutex);
            auto it = _measure_results.find(job_id);
            if(it != _measure_results.end())
            {
                it->second.failed = true;
                it->second.error = ex.what();
            }
        }

        lock_guard<mutex> lock(_measure_mutex);
        auto it = _measure_results.find(job_id);
        if(it != _measure_results.end())
        {
            it->second.percent_complete = 100;
            it->second.completed_at = steady_clock::now();
        }
        _measure_jobs.erase(job_id);

        // Drop results that were completed and never collected within 5 minutes.
        auto cutoff = steady_clock::now() - minutes(5);
        for(auto it2 = _measure_results.begin(); it2 != _measure_results.end(); )
        {
            if(it2->second.percent_complete == 100 && it2->second.completed_at < cutoff)
                it2 = _measure_results.erase(it2);
            else
                ++it2;
        }
    }
}

void r_ws::_measure(const std::string& job_id)
{
    measure_job job;
    {
        lock_guard<mutex> lock(_measure_mutex);
        auto it = _measure_jobs.find(job_id);
        if(it == _measure_jobs.end())
            R_THROW(("Measure job vanished: %s", job_id.c_str()));
        job = it->second;
    }

    auto maybe_camera = _devices.get_camera_by_id(job.camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", job.camera_id.c_str()));
    auto camera = maybe_camera.value();

    // Prefer the job's credentials; fall back to the camera's stored ones (a
    // manually-added camera carries the creds entered in the Add dialog).
    // get_camera_by_id returns them decrypted.
    r_nullable<string> username = job.username;
    r_nullable<string> password = job.password;
    if(username.is_null() && !camera.rtsp_username.is_null()) username = camera.rtsp_username;
    if(password.is_null() && !camera.rtsp_password.is_null()) password = camera.rtsp_password;

    if(!camera.xaddrs.is_null())
    {
        // ONVIF camera: interrogate the chosen profile — resolves the RTSP URL
        // and persists codec params into the devices DB (needed for measuring
        // and recording).
        if(camera.camera_name.is_null() || camera.ipv4.is_null() || camera.address.is_null())
            R_THROW(("Camera is missing discovery fields needed for interrogation."));

        _agent.interrogate_camera(
            camera.camera_name.value(),
            camera.ipv4.value(),
            camera.xaddrs.value(),
            camera.address.value(),
            username,
            password,
            job.profile_token,
            camera.onvif_port,
            camera.onvif_protocol
        );
    }
    else if(camera.rtsp_url.is_null())
    {
        R_THROW(("Camera has neither an ONVIF address nor an RTSP URL."));
    }
    // else: manually-added RTSP camera — query_measure_camera derives the codec
    // params from the stream's SDP, so no ONVIF interrogation is needed.

    // Stream ~15s to measure bitrate + grab a snapshot.
    auto m = query_measure_camera(_top_dir, _devices, job.camera_id, username, password, 320, 240);

    lock_guard<mutex> lock(_measure_mutex);
    auto it = _measure_results.find(job_id);
    if(it != _measure_results.end())
    {
        it->second.byte_rate   = m.byte_rate;
        it->second.video_codec = m.video_codec;
        it->second.jpeg        = std::move(m.jpeg);
    }
}

r_http::r_server_response r_ws::_get_measure_progress(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto args = request.get_uri().get_get_args();
        if(args.find("id") == args.end())
            R_THROW(("Missing id."));

        lock_guard<mutex> lock(_measure_mutex);
        auto it = _measure_results.find(args["id"]);
        if(it == _measure_results.end())
            R_STHROW(r_http_404_exception, ("Measure job not found: %s", args["id"].c_str()));

        json j;
        j["id"] = args["id"];
        j["percent_complete"] = it->second.percent_complete;
        j["failed"] = it->second.failed;
        if(it->second.failed)
            j["error"] = it->second.error;

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const r_http_404_exception&) { throw; }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to get measure progress."));
}

r_http::r_server_response r_ws::_get_measure_result(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto args = request.get_uri().get_get_args();
        if(args.find("id") == args.end())
            R_THROW(("Missing id."));
        auto id = args["id"];

        json j;
        {
            lock_guard<mutex> lock(_measure_mutex);
            auto it = _measure_results.find(id);
            if(it == _measure_results.end())
                R_STHROW(r_http_404_exception, ("Measure job not found: %s", id.c_str()));
            if(it->second.percent_complete < 100)
                R_STHROW(r_http_500_exception, ("Measurement not yet complete: %s", id.c_str()));
            if(it->second.failed)
                R_THROW(("Measurement failed: %s", it->second.error.c_str()));

            j["byte_rate"]   = it->second.byte_rate;
            j["video_codec"] = it->second.video_codec;
            j["jpeg_b64"]    = it->second.jpeg.empty()
                ? string()
                : r_string_utils::to_base64(it->second.jpeg.data(), it->second.jpeg.size());

            // One-shot: free the result (and its snapshot) once collected.
            _measure_results.erase(it);
        }

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const r_http_404_exception&) { throw; }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to get measure result."));
}

r_http::r_server_response r_ws::_post_configure_camera(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto req = json::parse(request.get_body_as_string());

        if(!req.contains("camera_id"))
            R_THROW(("Missing camera_id."));
        if(!req.contains("byte_rate"))
            R_THROW(("Missing byte_rate."));
        if(!req.contains("retention_days"))
            R_THROW(("Missing retention_days."));

        auto camera_id      = req["camera_id"].get<string>();
        auto friendly_name  = req.value("friendly_name", string());
        bool do_motion      = req.value("do_motion_detection", true);
        double retention    = req["retention_days"].get<double>();
        int64_t byte_rate   = req["byte_rate"].get<int64_t>();

        r_nullable<string> username, password;
        if(req.contains("username") && !req["username"].is_null()) username.set_value(req["username"].get<string>());
        if(req.contains("password") && !req["password"].is_null()) password.set_value(req["password"].get<string>());

        query_configure_camera(_top_dir, _devices, camera_id, username, password, friendly_name, do_motion, retention, byte_rate);

        json j;
        j["camera_id"] = camera_id;
        j["status"] = "assigned";

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to configure camera."));
}

r_http::r_server_response r_ws::_post_remove_camera(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto req = json::parse(request.get_body_as_string());

        if(!req.contains("camera_id"))
            R_THROW(("Missing camera_id."));

        auto camera_id    = req["camera_id"].get<string>();
        bool delete_files = req.value("delete_files", false);

        query_remove_camera(_top_dir, _devices, camera_id, delete_files);

        json j;
        j["camera_id"] = camera_id;
        j["status"] = "discovered";
        j["files_deleted"] = delete_files;

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to remove camera."));
}

r_http::r_server_response r_ws::_post_update_camera_properties(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto req = json::parse(request.get_body_as_string());

        if(!req.contains("camera_id"))
            R_THROW(("Missing camera_id."));

        auto camera_id     = req["camera_id"].get<string>();
        bool do_motion     = req.value("do_motion_detection", false);
        bool do_prune      = req.value("do_motion_pruning", false);
        int  min_hours     = req.value("min_continuous_recording_hours", 24);
        r_nullable<string> motion_tier;
        if(req.contains("motion_tier"))
            motion_tier.set_value(req["motion_tier"].get<string>());

        query_update_camera_properties(_top_dir, _devices, camera_id, do_motion, do_prune, min_hours, motion_tier);

        // Restart the recording context so the new settings take effect live —
        // the poll loop only re-applies stream-config changes, not these.
        _stream_keeper.bounce(camera_id);

        json j;
        j["camera_id"] = camera_id;
        j["status"] = "ok";

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to update camera properties."));
}

r_http::r_server_response r_ws::_post_forget_camera(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto req = json::parse(request.get_body_as_string());

        if(!req.contains("camera_id"))
            R_THROW(("Missing camera_id."));

        auto camera_id = req["camera_id"].get<string>();

        auto maybe_camera = _devices.get_camera_by_id(camera_id);
        if(maybe_camera.is_null())
            R_THROW(("Unknown camera id: %s", camera_id.c_str()));

        // Drop the camera's row and clear the discovery agent's memory of it.
        // If the camera is still broadcasting ONVIF it will be re-discovered on
        // the next poll; this mirrors the desktop "Forget" button.
        auto camera = maybe_camera.value();
        _devices.remove_camera(camera);
        _agent.forget(camera_id);

        json j;
        j["camera_id"] = camera_id;
        j["status"] = "forgotten";

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to forget camera."));
}

r_http::r_server_response r_ws::_post_add_rtsp_camera(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto req = json::parse(request.get_body_as_string());

        if(!req.contains("rtsp_url") || req["rtsp_url"].get<string>().empty())
            R_THROW(("Missing rtsp_url."));

        // Manually-added RTSP camera. Mirrors the desktop "Add RTSP Source
        // Camera" dialog: a bare camera row in the discovered state, identified
        // only by its RTSP URL (no ONVIF xaddrs).
        r_camera c;
        c.id = r_uuid::generate();
        c.state = "discovered";
        c.camera_name.set_value(req.value("camera_name", string()));
        if(req.contains("ipv4") && !req["ipv4"].get<string>().empty())
            c.ipv4.set_value(req["ipv4"].get<string>());
        c.rtsp_url.set_value(req["rtsp_url"].get<string>());

        auto username = req.value("rtsp_username", string());
        if(!username.empty())
        {
            c.rtsp_username.set_value(username);
            c.rtsp_password.set_value(req.value("rtsp_password", string()));
        }

        _devices.save_camera(c);

        json j;
        j["camera_id"] = c.id;
        j["status"] = "discovered";

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to add RTSP camera."));
}

// A thrown r_http exception reaches the client as a bare status code; the
// storage endpoints answer their user errors with {"error": message} instead,
// which the web UI shows.
static r_server_response _json_reply(const json& j, status_code code = response_ok)
{
    r_server_response response;
    if(code != response_ok)
        response.set_status_code(code);
    response.set_content_type("text/json");
    response.set_body(j.dump());
    return response;
}

static r_server_response _json_error(status_code code, const string& message)
{
    return _json_reply(json{{"error", message}}, code);
}

// The camera's recording bitrate, 0 when it isn't receiving video.
static int64_t _camera_byte_rate(r_stream_keeper& stream_keeper, const string& camera_id)
{
    for(auto& st : stream_keeper.fetch_stream_status())
        if(st.camera.id == camera_id)
            return st.bytes_per_second;
    return 0;
}

bool r_ws::_start_storage_op(const string& kind, const r_camera& camera, function<void(r_storage_op&)> work)
{
    lock_guard<mutex> g(_storage_mutex);
    if(_storage_op.running)
        return false;
    if(_storage_th.joinable())
        _storage_th.join();

    _storage_op.progress = 0.f;
    _storage_op.done = false;
    _storage_op.error = false;
    _storage_op.cancel_requested = false;
    _storage_op.set_status("");
    _storage_op.running = true;
    _storage_op_camera_id = camera.id;
    _storage_op_kind = kind;

    _storage_th = thread([this, work](){ work(_storage_op); });
    return true;
}

r_http::r_server_response r_ws::_get_storage_info(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto args = request.get_uri().get_get_args();
        if(args.find("camera_id") == args.end())
            return _json_error(response_bad_request, "Missing camera_id.");

        auto maybe_camera = _devices.get_camera_by_id(args["camera_id"]);
        if(maybe_camera.is_null())
            return _json_error(response_not_found, "Unknown camera.");
        auto camera = maybe_camera.value();

        int64_t bps = _camera_byte_rate(_stream_keeper, camera.id);
        int64_t nb = camera.n_record_file_blocks.is_null() ? 0 : camera.n_record_file_blocks.value();
        int64_t bs = camera.record_file_block_size.is_null() ? 0 : camera.record_file_block_size.value();

        json j;
        j["camera_id"] = camera.id;
        j["record_file_path"] = camera.record_file_path.is_null() ? string() : storage_path(_top_dir, camera.record_file_path.value());
        j["byte_rate"] = bps;
        j["file_size"] = nb * bs;
        j["file_size_text"] = human_readable_file_size((double)(nb * bs));
        // The days the file holds at the current bitrate (as the desktop shows).
        j["retention_days"] = (bps > 0 && nb > 0 && bs > 0) ? ((double)(nb * bs) / (double)bps / 86400.0) : 0.0;

        // With days: the file size Change retention would make, for the dialog.
        if(args.find("days") != args.end())
        {
            double days = r_string_utils::s_to_double(args["days"]);
            if(bps > 0 && days > 0.0)
            {
                auto sz = required_file_size_for_retention_hours((int64_t)(days * 24.0), bps);
                j["new_file_size"] = sz.first * sz.second;
                j["new_file_size_text"] = human_readable_file_size((double)(sz.first * sz.second));
            }
        }

        return _json_reply(j);
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to get storage info."));
}

r_http::r_server_response r_ws::_post_storage_move(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto req = json::parse(request.get_body_as_string());

        auto maybe_camera = _devices.get_camera_by_id(req.value("camera_id", string()));
        if(maybe_camera.is_null())
            return _json_error(response_not_found, "Unknown camera.");
        auto camera = maybe_camera.value();
        if(camera.record_file_path.is_null())
            return _json_error(response_bad_request, "This camera has no recording file.");

        auto new_dir = r_string_utils::strip(req.value("new_dir", string()));
        bool start_fresh = req.value("start_fresh", false);
        bool delete_confirmed = req.value("delete_confirmed", false);

        // A folder on the machine Revere runs on, which the web UI can't browse.
        std::error_code ec;
        if(new_dir.empty() || !std::filesystem::path(new_dir).is_absolute())
            return _json_error(response_bad_request, "Enter the full path of a folder on the machine Revere runs on.");
        if(!std::filesystem::is_directory(new_dir, ec))
            return _json_error(response_bad_request, "There's no folder at " + new_dir + " on the machine Revere runs on.");
        if(start_fresh && !delete_confirmed)
            return _json_error(response_bad_request, "Starting fresh deletes this camera's recordings; confirm that first.");

        auto current_dir = std::filesystem::path(storage_path(_top_dir, camera.record_file_path.value())).parent_path();
        if(!start_fresh && std::filesystem::equivalent(current_dir, new_dir, ec))
            return _json_error(response_bad_request, "The recording files are already in that folder.");

        auto top_dir = _top_dir;
        auto& stream_keeper = _stream_keeper;
        auto& devices = _devices;
        bool started = _start_storage_op(start_fresh ? "reset" : "move", camera, [top_dir, &stream_keeper, &devices, camera, new_dir, start_fresh](r_storage_op& op){
            if(start_fresh)
                storage_reset(top_dir, stream_keeper, devices, camera, new_dir, op);
            else
                storage_move(top_dir, stream_keeper, devices, camera, new_dir, op);
        });
        if(!started)
            return _json_error(response_bad_request, "Another storage operation is still running.");

        return _json_reply(json{{"status", "started"}});
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to start moving storage."));
}

r_http::r_server_response r_ws::_post_storage_change_retention(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto req = json::parse(request.get_body_as_string());

        auto maybe_camera = _devices.get_camera_by_id(req.value("camera_id", string()));
        if(maybe_camera.is_null())
            return _json_error(response_not_found, "Unknown camera.");
        auto camera = maybe_camera.value();
        if(camera.record_file_path.is_null())
            return _json_error(response_bad_request, "This camera has no recording file.");

        double days = req.value("days", 0.0);
        if(!req.value("delete_confirmed", false))
            return _json_error(response_bad_request, "Resizing deletes this camera's recordings; confirm that first.");
        if(!(days > 0.0))
            return _json_error(response_bad_request, "Enter a number of days greater than 0.");

        int64_t bps = _camera_byte_rate(_stream_keeper, camera.id);
        if(bps <= 0)
            return _json_error(response_bad_request, "This camera isn't receiving video, so there's no bitrate to size the file from.");

        auto sz = required_file_size_for_retention_hours((int64_t)(days * 24.0), bps);

        auto top_dir = _top_dir;
        auto& stream_keeper = _stream_keeper;
        auto& devices = _devices;
        bool started = _start_storage_op("retention", camera, [top_dir, &stream_keeper, &devices, camera, sz](r_storage_op& op){
            storage_change_retention(top_dir, stream_keeper, devices, camera, sz.first, sz.second, op);
        });
        if(!started)
            return _json_error(response_bad_request, "Another storage operation is still running.");

        return _json_reply(json{{"status", "started"}});
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to start changing retention."));
}

r_http::r_server_response r_ws::_get_storage_op(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    // running until the operation ends; then error, or done (a move cancelled
    // part way is done, with the status saying so).
    json j;
    {
        lock_guard<mutex> g(_storage_mutex);
        j["kind"] = _storage_op_kind;
        j["camera_id"] = _storage_op_camera_id;
    }
    j["running"] = _storage_op.running.load();
    j["done"] = _storage_op.done.load();
    j["error"] = _storage_op.error.load();
    j["progress"] = _storage_op.progress.load();
    j["status"] = _storage_op.get_status();
    return _json_reply(j);
}

// A plugin status colour as one of the status tones the UIs draw ("ok",
// "warning", "error", "neutral"), as the desktop's plugin_status_color does: a
// name, "#rrggbb" or [r, g, b]; a colour by hue, and anything grey, dark or
// outside green/amber/red is neutral.
static string _plugin_status_tone(const json& c)
{
    double r = 0, g = 0, b = 0;
    if(c.is_string())
    {
        auto n = r_string_utils::to_lower(c.get<string>());
        if(n == "green" || n == "ok" || n == "success") return "ok";
        if(n == "yellow" || n == "amber" || n == "orange" || n == "warn" || n == "warning") return "warning";
        if(n == "red" || n == "error" || n == "danger") return "error";
        if(n.size() != 7 || n[0] != '#' || !all_of(n.begin() + 1, n.end(), [](unsigned char ch) { return isxdigit(ch); }))
            return "neutral";
        auto v = stoul(n.substr(1), nullptr, 16);
        r = ((v >> 16) & 0xff) / 255.0; g = ((v >> 8) & 0xff) / 255.0; b = (v & 0xff) / 255.0;
    }
    else if(c.is_array() && c.size() >= 3 && c[0].is_number() && c[1].is_number() && c[2].is_number())
    {
        auto ch = [&](size_t i) { return clamp(c[i].get<int>(), 0, 255) / 255.0; };
        r = ch(0); g = ch(1); b = ch(2);
    }
    else
        return "neutral";

    double mx = max({r, g, b}), mn = min({r, g, b}), d = mx - mn;
    double sat = mx > 0 ? d / mx : 0, val = mx;
    if(sat < 0.25 || val < 0.2)
        return "neutral";
    double deg = (mx == r) ? 60.0 * fmod((g - b) / d, 6.0) : (mx == g) ? 60.0 * ((b - r) / d + 2.0) : 60.0 * ((r - g) / d + 4.0);
    if(deg < 0) deg += 360.0;
    if(deg >= 75.0 && deg < 170.0) return "ok";
    if(deg >= 20.0 && deg < 75.0) return "warning";
    if(deg < 20.0 || deg >= 330.0) return "error";
    return "neutral";
}

// A QR code as SVG, with the 4-module quiet zone; empty if the text doesn't fit.
static string _qr_svg(const string& text)
{
    auto qr = r_qr_code::encode(text);
    if(qr.size == 0)
        return string();
    const int quiet = 4, span = qr.size + quiet * 2;
    string path;
    for(int y = 0; y < qr.size; ++y)
        for(int x = 0; x < qr.size; ++x)
            if(qr.dark(x, y))
                path += r_string_utils::format("M%d %dh1v1h-1z", x + quiet, y + quiet);
    return r_string_utils::format("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" shape-rendering=\"crispEdges\">"
                                  "<rect width=\"%d\" height=\"%d\" fill=\"#fff\"/><path d=\"%s\" fill=\"#000\"/></svg>",
                                  span, span, span, span, path.c_str());
}

// The system plugins, as the desktop's Plugins section shows them:
//   {"plugins": [{"name", "display_name", "version", "enabled",
//                 "status": {"label", "tone"} or null, "lines": [...]}]}
// lines are the plugin's status message lines as it gives them (text, with url
// or copy; tiles; qr), a qr line also carrying its code drawn as "qr_svg".
// Authenticated, unlike /version: a plugin's lines can hold a pairing code.
r_http::r_server_response r_ws::_get_system_plugins(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        json plugins = json::array();
        for(const auto& name : _stream_keeper.get_loaded_system_plugins())
        {
            json p;
            p["name"] = name;
            p["display_name"] = "";
            p["version"] = _stream_keeper.get_system_plugin_version(name);
            p["enabled"] = _stream_keeper.is_system_plugin_enabled(name);
            p["status"] = nullptr;
            p["lines"] = json::array();

            // A plugin's JSON that doesn't parse is left out, as on the desktop.
            auto status = _stream_keeper.get_system_plugin_status(name);
            if(!status.empty())
            {
                auto sj = json::parse(status, nullptr, false);
                if(sj.is_object())
                {
                    if(sj.contains("name") && sj["name"].is_string())
                        p["display_name"] = sj["name"];
                    p["status"] = {{"label", sj.value("label", string("Unknown"))},
                                   {"tone", sj.contains("color") ? _plugin_status_tone(sj["color"]) : string("neutral")}};
                }
            }

            auto message = _stream_keeper.get_system_plugin_status_message(name);
            if(!message.empty())
            {
                auto mj = json::parse(message, nullptr, false);
                if(mj.is_object() && mj.contains("lines") && mj["lines"].is_array())
                {
                    for(auto line : mj["lines"])
                    {
                        if(!line.is_object())
                            continue;
                        if(line.contains("qr") && line["qr"].is_string())
                            line["qr_svg"] = _qr_svg(line["qr"].get<string>());
                        p["lines"].push_back(line);
                    }
                }
            }

            plugins.push_back(p);
        }
        return _json_reply(json{{"plugins", plugins}});
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to get system plugins."));
}

r_http::r_server_response r_ws::_post_system_plugin_enabled(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto req = json::parse(request.get_body_as_string(), nullptr, false);
        if(!req.is_object() || !req.contains("name") || !req["name"].is_string() || !req.contains("enabled") || !req["enabled"].is_boolean())
            return _json_error(response_bad_request, "Expected {\"name\": ..., \"enabled\": true|false}.");

        auto name = req["name"].get<string>();
        auto loaded = _stream_keeper.get_loaded_system_plugins();
        if(find(loaded.begin(), loaded.end(), name) == loaded.end())
            return _json_error(response_not_found, "No plugin named " + name + ".");

        _stream_keeper.set_system_plugin_enabled(name, req["enabled"].get<bool>());
        return _json_reply(json{{"name", name}, {"enabled", _stream_keeper.is_system_plugin_enabled(name)}});
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to set the plugin's enabled state."));
}

static json _keep_awake_json(const r_keep_awake_status& s)
{
    json events = json::array();
    for(const auto& e : s.events)
        events.push_back({{"what", e.what}, {"start_ms", e.start_ms}, {"end_ms", e.end_ms}});
    return json{
        {"enabled", s.settings.enabled},
        {"on_battery", s.settings.on_battery},
        {"power", power_source_name(s.power)},
        {"wanted", s.wanted},
        {"held", s.held},
        {"problem", s.problem},
        {"unavailable", s.unavailable},
        {"events", events}
    };
}

// {"enabled", "on_battery", "power": "ac"|"battery"|"unknown", "wanted",
// "held", "problem", "unavailable", "events": [{"what", "start_ms", "end_ms"}]
// newest first}.
r_http::r_server_response r_ws::_get_keep_awake(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        return _json_reply(_keep_awake_json(_stream_keeper.keep_awake().get_status()));
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to read the keep awake setting."));
}

// Either or both of {"enabled": bool, "on_battery": bool}; replies as GET.
r_http::r_server_response r_ws::_post_keep_awake(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    try
    {
        auto req = json::parse(request.get_body_as_string(), nullptr, false);
        auto is_bool = [&](const char* k) { return req.contains(k) && req[k].is_boolean(); };
        if(!req.is_object() || (!is_bool("enabled") && !is_bool("on_battery")) ||
           (req.contains("enabled") && !is_bool("enabled")) || (req.contains("on_battery") && !is_bool("on_battery")))
            return _json_error(response_bad_request, "Expected {\"enabled\": true|false, \"on_battery\": true|false}.");

        auto& ka = _stream_keeper.keep_awake();
        auto settings = ka.get_settings();
        if(is_bool("enabled"))
            settings.enabled = req["enabled"].get<bool>();
        if(is_bool("on_battery"))
            settings.on_battery = req["on_battery"].get<bool>();
        ka.set_settings(settings);
        return _json_reply(_keep_awake_json(ka.get_status()));
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to change the keep awake setting."));
}

r_http::r_server_response r_ws::_post_storage_op_cancel(r_utils::r_socket_base&, const r_http::r_server_request& request)
{
    _require_auth(request);

    // Only a move's copy between filesystems can stop part way.
    if(_storage_op.running)
        _storage_op.cancel_requested = true;
    return _json_reply(json{{"status", "ok"}});
}

std::string r_ws::_password_path() const
{
    return _top_dir + PATH_SLASH + "system_password";
}

bool r_ws::_system_password_set() const
{
    return r_fs::file_exists(_password_path());
}

r_utils::r_nullable<std::string> r_ws::_load_system_password()
{
    r_nullable<string> out;
    if(_master_key.empty() || !r_fs::file_exists(_password_path()))
        return out;
    auto bytes = r_fs::read_file(_password_path());
    string enc(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    out.set_value(r_credential_crypto::decrypt_credential(enc, _master_key));
    return out;
}

void r_ws::_store_system_password(const std::string& password)
{
    if(_master_key.empty())
        R_THROW(("Secure storage unavailable; cannot set the local web password."));
    auto enc = r_credential_crypto::encrypt_credential(password, _master_key);
    // Write to a temp file then atomically rename, so a crash mid-write can't
    // leave a truncated (undecryptable) password file.
    auto tmp = _password_path() + ".tmp";
    r_fs::write_file(reinterpret_cast<const uint8_t*>(enc.data()), enc.size(), tmp);
    r_fs::atomic_rename_file(tmp, _password_path());
}

void r_ws::set_system_password(const std::string& password)
{
    if(password.empty())
        R_THROW(("The local web password must not be empty."));
    _store_system_password(password);
    // Existing web sessions must re-authenticate against the new password.
    _auth.revoke_all_sessions();
}

bool r_ws::system_password_set() const
{
    return _system_password_set();
}

void r_ws::clear_system_password()
{
    auto path = _password_path();
    if(r_fs::file_exists(path))
        r_fs::remove_file(path);
    // Invalidate any outstanding web sessions.
    _auth.revoke_all_sessions();
}

bool r_ws::_token_valid(const r_http::r_server_request& request)
{
    auto token = _request_token(request);
    return !token.is_null() && _auth.valid(token.value());
}

void r_ws::_require_auth(const r_http::r_server_request& request)
{
    if(!_token_valid(request))
        R_STHROW(r_http_401_exception, ("Unauthorized."));
}

r_http::r_server_response r_ws::_get_auth_status(r_utils::r_socket_base&, const r_http::r_server_request&)
{
    // Unauthenticated on purpose: lets the web UI decide between a first-run
    // "create password" screen and a "login" screen. Only leaks whether a
    // system password has been configured.
    json j;
    j["password_set"] = _system_password_set();

    r_server_response response;
    response.set_content_type("text/json");
    response.set_body(j.dump());
    return response;
}

r_http::r_server_response r_ws::_post_login(r_utils::r_socket_base& conn, const r_http::r_server_request& request)
{
    auto address = _peer_ip(conn);
    try
    {
        auto req = json::parse(request.get_body_as_string());
        if(!req.contains("password") || !req["password"].is_string())
            R_STHROW(r_http_400_exception, ("Missing password."));

        // Refuse while this address is locked out, whatever it sends: bounds
        // guessing to a few attempts per cooldown.
        if(_auth.locked_out(address))
            R_STHROW(r_http_401_exception, ("Too many failed attempts; try again later."));

        auto stored = _load_system_password();
        if(stored.is_null())
            R_STHROW(r_http_401_exception, ("No local web password is set."));

        if(!r_ws_auth::constant_time_equal(req["password"].get<string>(), stored.value()))
        {
            _auth.record_failure(address);
            R_LOG_WARNING("r_ws: failed login from %s", address.empty() ? "?" : address.c_str());
            R_STHROW(r_http_401_exception, ("Invalid password."));
        }

        _auth.record_success(address);
        auto token = _auth.issue_session();

        json j;
        j["token"] = token;

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        // Browsers also get it as a cookie, which their <img>/<video> loads and
        // downloads send automatically (they can't set a bearer header).
        response.add_additional_header("Set-Cookie", r_ws_auth::session_cookie(token, _is_tls(conn)));
        return response;
    }
    catch(const r_http_400_exception&) { throw; }
    catch(const r_http_401_exception&) { throw; }
    catch(const json::exception&)
    {
        R_STHROW(r_http_400_exception, ("Malformed login request."));
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Login failed."));
}

r_http::r_server_response r_ws::_post_session(r_utils::r_socket_base& conn, const r_http::r_server_request& request)
{
    // Reached only with a valid token (the route requires one). Only a bearer
    // token is accepted here (POST never honours the cookie), and only a
    // session token is turned into a cookie: the local token stays out of
    // browsers.
    auto token = _request_token(request);
    if(token.is_null() || r_ws_auth::constant_time_equal(token.value(), _auth.local_token()))
        R_STHROW(r_http_400_exception, ("A session token is required."));

    r_server_response response(response_no_content);
    response.add_additional_header("Set-Cookie", r_ws_auth::session_cookie(token.value(), _is_tls(conn)));
    return response;
}

r_http::r_server_response r_ws::_get_rtsp_credentials(r_utils::r_socket_base&, const r_http::r_server_request&)
{
    json j;
    j["username"] = r_rtsp_auth::RTSP_USER;
    j["password"] = _stream_keeper.rtsp_password();
    j["port"] = 8554;

    r_server_response response;
    response.set_content_type("text/json");
    response.set_body(j.dump());
    response.add_additional_header("Cache-Control", "no-store");
    return response;
}

r_http::r_server_response r_ws::_post_logout(r_utils::r_socket_base& conn, const r_http::r_server_request& request)
{
    // Open route: logging out with an expired or missing token still clears
    // the cookie. The cookie is read here even though this is a POST: ending a
    // session is harmless to forge.
    auto token = r_ws_auth::extract_token(request.get_header("authorization"), request.get_header("cookie"), true);
    if(!token.is_null())
        _auth.revoke(token.value());

    r_server_response response(response_no_content);
    response.add_additional_header("Set-Cookie", r_ws_auth::clear_session_cookie(_is_tls(conn)));
    return response;
}

r_http::r_server_response r_ws::_post_set_password(r_utils::r_socket_base& conn, const r_http::r_server_request& request)
{
    auto address = _peer_ip(conn);
    try
    {
        auto req = json::parse(request.get_body_as_string());
        if(!req.contains("new_password") || !req["new_password"].is_string())
            R_STHROW(r_http_400_exception, ("Missing new_password."));
        auto new_password = req["new_password"].get<string>();
        if(new_password.empty())
            R_STHROW(r_http_400_exception, ("Password must not be empty."));

        if(!_system_password_set())
        {
            // Bootstrap: the first password needs no credentials, but only
            // from this machine. Otherwise anyone who could reach the HTTPS
            // port of an unconfigured install could claim it.
            if(!r_local_api_token::is_loopback_host(address))
                R_STHROW(r_http_403_exception, ("The first password must be set on the Revere machine itself."));
        }
        else
        {
            // A change needs a valid token or the current password. The
            // current-password path counts toward the same per-address
            // lockout as /login, so it can't be used to guess around it.
            bool authorized = _token_valid(request);
            if(!authorized)
            {
                if(_auth.locked_out(address))
                    R_STHROW(r_http_401_exception, ("Too many failed attempts; try again later."));
                if(req.contains("current_password") && req["current_password"].is_string())
                {
                    auto stored = _load_system_password();
                    authorized = !stored.is_null() && r_ws_auth::constant_time_equal(req["current_password"].get<string>(), stored.value());
                    if(authorized)
                        _auth.record_success(address);
                    else _auth.record_failure(address);
                }
            }
            if(!authorized)
                R_STHROW(r_http_401_exception, ("Provide a valid token or current_password to change the local web password."));
        }

        _store_system_password(new_password);

        // Every outstanding session must sign in again with the new password.
        _auth.revoke_all_sessions();

        json j;
        j["status"] = "ok";

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        // The caller's own cookie (if any) died with its session.
        response.add_additional_header("Set-Cookie", r_ws_auth::clear_session_cookie(_is_tls(conn)));
        return response;
    }
    catch(const r_http_400_exception&) { throw; }
    catch(const r_http_401_exception&) { throw; }
    catch(const r_http_403_exception&) { throw; }
    catch(const json::exception&)
    {
        R_STHROW(r_http_400_exception, ("Malformed set_password request."));
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to set the local web password."));
}

r_server_response r_ws::_post_mcp(r_socket_base&, const r_server_request& request)
{
    json id = nullptr;
    try
    {
        auto req = json::parse(request.get_body_as_string());

        // Notifications have no id - acknowledge without a body
        if(!req.contains("id"))
        {
            r_server_response resp;
            resp.set_content_type("application/json");
            resp.set_body("{}");
            return resp;
        }

        id = req["id"];
        string method = req.value("method", "");

        if(method == "initialize")
        {
            return mcp_ok(id, {
                {"protocolVersion", "2024-11-05"},
                {"capabilities", {{"tools", json::object()}}},
                {"serverInfo", {{"name", "revere"}, {"version", revere_version()}}}
            });
        }

        if(method == "tools/list")
        {
            auto tools = json::array();

            tools.push_back({
                {"name", "list_cameras"},
                {"description", "Returns all configured cameras with their IDs, friendly names, IP addresses, and recording state. Always call this first in a session to discover available camera IDs before querying any other tools."},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", json::object()},
                    {"required", json::array()}
                }}
            });

            tools.push_back({
                {"name", "get_recording_segments"},
                {"description", "Returns contiguous recording segments for a camera in a time range. Gaps between segments indicate missing footage. Use this to verify that recording exists before querying frames or analytics for a specific time window."},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", {
                        {"camera_id", {{"type","string"},{"description","Camera ID from list_cameras"}}},
                        {"start_time", {{"type","string"},{"description","ISO-8601 start timestamp e.g. 2024-01-15T12:00:00Z"}}},
                        {"end_time",   {{"type","string"},{"description","ISO-8601 end timestamp"}}}
                    }},
                    {"required", json::array({"camera_id","start_time","end_time"})}
                }}
            });

            tools.push_back({
                {"name", "get_motion_events"},
                {"description", "Returns motion detection events (pixel-level) for a camera in a time range. "
                                "PREFERRED FIRST PASS: Use this as a cheap index before calling get_frame_image or get_analytics. "
                                "Query a large time window here to quickly find WHEN activity occurred, then zoom in with other tools. "
                                "For 'did X get left behind?' questions (e.g. package on porch, car parked): call this first, then call "
                                "get_frame_image at motion_end_time -- that frame shows the scene after the person/vehicle left, revealing anything deposited. "
                                "For cross-camera flow detection: query all cameras over the same window, sort events by start_time, "
                                "then use get_frame_image to visually confirm whether nearby events on different cameras show the same object moving through the scene."},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", {
                        {"camera_id",        {{"type","string"},  {"description","Camera ID from list_cameras"}}},
                        {"start_time",       {{"type","string"},  {"description","ISO-8601 start timestamp"}}},
                        {"end_time",         {{"type","string"},  {"description","ISO-8601 end timestamp"}}},
                    }},
                    {"required", json::array({"camera_id","start_time","end_time"})}
                }}
            });

            tools.push_back({
                {"name", "get_analytics"},
                {"description", "Returns AI object detection events (persons, vehicles, etc.) for a camera in a time range. "
                                "Each event has a timestamp (when the object was first identified by the neural net), "
                                "motion_start_time (when pixel motion began, may be earlier), motion_end_time, "
                                "and a detections array with per-frame class, confidence, and timestamp. "
                                "Use get_motion_events first to find the right time window, then call this to get object-level detail. "
                                "For 'who/what was there?' questions use this; for 'when did anything move?' use get_motion_events. "
                                "For highlight reel or flow detection across cameras: query all cameras, merge and sort by timestamp, "
                                "then group nearby events that may represent the same object moving across camera views."},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", {
                        {"camera_id",  {{"type","string"}, {"description","Camera ID from list_cameras"}}},
                        {"start_time", {{"type","string"}, {"description","ISO-8601 start timestamp"}}},
                        {"end_time",   {{"type","string"}, {"description","ISO-8601 end timestamp"}}},
                        {"stream_tag", {{"type","string"}, {"description","Optional: filter by analytics stream tag e.g. person_metadata"}}}
                    }},
                    {"required", json::array({"camera_id","start_time","end_time"})}
                }}
            });

            tools.push_back({
                {"name", "get_frame_image"},
                {"description", "Returns a JPEG image from a camera at a specific timestamp for visual inspection. "
                                "Always use millisecond-precision timestamps from analytics or motion events -- rounding to the second can return the wrong keyframe. "
                                "For 'what was left behind?' questions: use motion_end_time from get_motion_events (shows the scene after activity ended). "
                                "For 'what triggered this event?': use the detection timestamp from get_analytics detections array. "
                                "For cross-camera flow confirmation: fetch frames from each camera at their respective event timestamps and visually compare to determine if the same object (vehicle color/type, person appearance) appears across views."},
                {"inputSchema", {
                    {"type", "object"},
                    {"properties", {
                        {"camera_id",  {{"type","string"},  {"description","Camera ID from list_cameras"}}},
                        {"start_time", {{"type","string"},  {"description","ISO-8601 timestamp of the frame to retrieve — use full millisecond precision from event data"}}},
                        {"width",      {{"type","integer"}, {"description","Output width in pixels (default 640)"}}},
                        {"height",     {{"type","integer"}, {"description","Output height in pixels (default 480)"}}}
                    }},
                    {"required", json::array({"camera_id","start_time"})}
                }}
            });

            return mcp_ok(id, {{"tools", tools}});
        }

        if(method == "tools/call")
        {
            string name = req["params"]["name"].get<string>();
            json args = req["params"].value("arguments", json::object());

            if(name == "list_cameras")
            {
                auto cameras = query_get_cameras(_devices);
                json j;
                j["cameras"] = json::array();
                for(auto& c : cameras)
                {
                    j["cameras"].push_back({
                        {"id",                 c.id},
                        {"camera_name",        c.camera_name.is_null()   ? "" : c.camera_name.value()},
                        {"friendly_name",      c.friendly_name.is_null() ? "" : c.friendly_name.value()},
                        {"ipv4",               c.ipv4.is_null()          ? "" : c.ipv4.value()},
                        {"state",              c.state},
                        {"do_motion_detection", !c.do_motion_detection.is_null() && c.do_motion_detection.value()}
                    });
                }
                return mcp_ok(id, {{"content", json::array({{{"type","text"},{"text",j.dump(2)}}})}});
            }

            if(name == "get_recording_segments")
            {
                auto start_tp = r_time_utils::iso_8601_to_tp(args["start_time"].get<string>());
                auto end_tp   = r_time_utils::iso_8601_to_tp(args["end_time"].get<string>());
                auto result   = query_get_contents(_top_dir, _devices, args["camera_id"].get<string>(), start_tp, end_tp);
                json j;
                j["segments"] = json::array();
                for(auto& s : result.segments)
                {
                    j["segments"].push_back({
                        {"start_time", r_time_utils::tp_to_iso_8601(s.start, true)},
                        {"end_time",   r_time_utils::tp_to_iso_8601(s.end,   true)}
                    });
                }
                return mcp_ok(id, {{"content", json::array({{{"type","text"},{"text",j.dump(2)}}})}});
            }

            if(name == "get_motion_events")
            {
                auto start_tp = r_time_utils::iso_8601_to_tp(args["start_time"].get<string>());
                auto end_tp   = r_time_utils::iso_8601_to_tp(args["end_time"].get<string>());
                auto events   = query_get_motion_events(_top_dir, _devices, args["camera_id"].get<string>(), start_tp, end_tp);
                json j;
                j["motion_events"] = json::array();
                for(auto& e : events)
                {
                    j["motion_events"].push_back({
                        {"start_time", r_time_utils::tp_to_iso_8601(e.start, true)},
                        {"end_time",   r_time_utils::tp_to_iso_8601(e.end,   true)},
                        {"motion",     e.motion},
                        {"avg_motion", e.avg_motion},
                        {"stddev",     e.stddev}
                    });
                }
                return mcp_ok(id, {{"content", json::array({{{"type","text"},{"text",j.dump(2)}}})}});
            }

            if(name == "get_analytics")
            {
                auto start_tp = r_time_utils::iso_8601_to_tp(args["start_time"].get<string>());
                auto end_tp   = r_time_utils::iso_8601_to_tp(args["end_time"].get<string>());
                r_nullable<string> stream_tag;
                if(args.contains("stream_tag") && !args["stream_tag"].is_null())
                    stream_tag.set_value(args["stream_tag"].get<string>());
                auto entries = query_get_analytics(_top_dir, _devices, args["camera_id"].get<string>(), start_tp, end_tp, stream_tag);
                json j;
                j["analytics"] = json::array();
                for(const auto& entry : entries)
                {
                    try
                    {
                        auto ts  = system_clock::time_point(milliseconds(entry.timestamp_ms));
                        auto parsed = json::parse(entry.json_data);
                        json item = {{"timestamp", r_time_utils::tp_to_iso_8601(ts, true)}};
                        item["data"] = parsed.contains("analytics") ? parsed["analytics"] : parsed;
                        j["analytics"].push_back(item);
                    }
                    catch(...) {}
                }
                return mcp_ok(id, {{"content", json::array({{{"type","text"},{"text",j.dump(2)}}})}});
            }

            if(name == "get_frame_image")
            {
                auto start_tp = r_time_utils::iso_8601_to_tp(args["start_time"].get<string>());
                uint16_t w = (uint16_t)args.value("width",  640);
                uint16_t h = (uint16_t)args.value("height", 480);
                auto camera_id  = args["camera_id"].get<string>();
                auto start_time = args["start_time"].get<string>();
                auto jpeg = query_get_jpg(_top_dir, _devices, camera_id, start_tp, w, h);
                auto b64  = r_string_utils::to_base64(jpeg.data(), jpeg.size());
                auto url  = string("http://localhost:8088/jpg?camera_id=") + camera_id
                          + "&start_time=" + start_time
                          + "&width=" + to_string(w)
                          + "&height=" + to_string(h);
                return mcp_ok(id, {{"content", json::array({
                    {{"type","image"},{"data",b64},{"mimeType","image/jpeg"}},
                    {{"type","text"},{"text","Frame URL: " + url}}
                })}});
            }

            return mcp_err(id, -32602, "Unknown tool: " + name);
        }

        return mcp_err(id, -32601, "Method not found: " + method);
    }
    catch(const exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
        return mcp_err(id, -32603, string("Internal error: ") + ex.what());
    }
}
