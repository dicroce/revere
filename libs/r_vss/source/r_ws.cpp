
#include "r_vss/r_ws.h"
#include "r_vss/r_stream_keeper.h"
#include "r_vss/r_query.h"
#include "r_vss/r_vss_utils.h"
#include "r_vss/r_motion_engine.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_file.h"
#include "r_utils/r_blob_tree.h"
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

// Returns the camera's source video resolution by parsing the SPS from the
// codec params stored alongside the latest recorded key frame. We can't use
// r_camera::video_codec_parameters directly — those come from the discovery-
// time SDP and frequently lack sprop-parameter-sets for cameras that only
// emit SPS/PPS inline in the bitstream. The stored params, by contrast, are
// populated by r_recording_context with the actual sprop bytes from the
// running stream, so they're always present for any camera that's recorded
// at least one key frame. Null on any failure (codec unknown, no recording,
// SPS missing) — resolution is best-effort metadata.
r_utils::r_nullable<std::pair<uint16_t, uint16_t>> _camera_source_resolution(const std::string& top_dir, const r_disco::r_camera& c)
{
    r_utils::r_nullable<std::pair<uint16_t, uint16_t>> out;
    if(c.record_file_path.is_null())
        return out;
    try
    {
        // Path resolution mirrors _get_storage_path() in r_query.cpp.
        const auto& rfp = c.record_file_path.value();
        std::string path = (rfp.find('/') != std::string::npos || rfp.find('\\') != std::string::npos)
            ? rfp
            : (top_dir + PATH_SLASH + "video" + PATH_SLASH + rfp);

        r_storage::r_storage_file_reader sf(path);
        auto last_ts = sf.last_ts();
        if(last_ts.is_null())
            return out;

        auto key_bt_buf = sf.query_key(R_STORAGE_MEDIA_TYPE_VIDEO, last_ts.value());
        uint32_t version = 0;
        auto bt = r_utils::r_blob_tree::deserialize(key_bt_buf.data(), key_bt_buf.size(), version);

        if(!bt.has_key("video_codec_name") || !bt.has_key("video_codec_parameters"))
            return out;
        auto codec = bt["video_codec_name"].get_string();
        auto params = bt["video_codec_parameters"].get_string();

        if(codec == "h264")
        {
            auto sps = r_pipeline::get_h264_sps(params);
            if(!sps.is_null())
            {
                auto info = r_pipeline::parse_h264_sps(sps.value());
                out.set_value({info.width, info.height});
            }
        }
        else if(codec == "h265")
        {
            auto sps = r_pipeline::get_h265_sps(params);
            if(!sps.is_null())
            {
                auto info = r_pipeline::parse_h265_sps(sps.value());
                out.set_value({info.width, info.height});
            }
        }
    }
    catch(...) { /* swallow — resolution is best-effort metadata */ }
    return out;
}

} // namespace

r_ws::r_ws(const string& top_dir, r_devices& devices, r_agent& agent, r_stream_keeper& stream_keeper) :
    _top_dir(top_dir),
    _devices(devices),
    _agent(agent),
    _stream_keeper(stream_keeper),
    _server(WEB_SERVER_PORT)
{
    _server.add_route(METHOD_GET, "/cameras", std::bind(&r_ws::_get_cameras, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/contents", std::bind(&r_ws::_get_contents, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/motion_events", std::bind(&r_ws::_get_motion_events, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/analytics", std::bind(&r_ws::_get_analytics, this, _1, _2, _3));

    _server.add_route(METHOD_GET, "/key_frame", std::bind(&r_ws::_get_key_frame, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/video", std::bind(&r_ws::_get_video, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/jpg", std::bind(&r_ws::_get_jpg, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/webp", std::bind(&r_ws::_get_webp, this, _1, _2, _3));

    _server.add_route(METHOD_GET, "/create_transcode_stream", std::bind(&r_ws::_get_create_transcode_stream, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/transcode", std::bind(&r_ws::_get_transcode, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/finalize_transcode_stream", std::bind(&r_ws::_get_finalize_transcode_stream, this, _1, _2, _3));

    _server.add_route(METHOD_GET, "/export", std::bind(&r_ws::_get_export, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/transcode_export", std::bind(&r_ws::_get_transcode_export, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/export_progress", std::bind(&r_ws::_get_export_progress, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/export_download", std::bind(&r_ws::_get_export_download, this, _1, _2, _3));

    _server.add_route(METHOD_POST, "/mcp", std::bind(&r_ws::_post_mcp, this, _1, _2, _3));

    // "Record" web flow: discover profiles, measure bitrate + snapshot, then
    // configure the camera for recording.
    _server.add_route(METHOD_GET, "/camera_profiles", std::bind(&r_ws::_get_camera_profiles, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/measure_camera", std::bind(&r_ws::_get_measure_camera, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/measure_progress", std::bind(&r_ws::_get_measure_progress, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/measure_result", std::bind(&r_ws::_get_measure_result, this, _1, _2, _3));
    _server.add_route(METHOD_POST, "/configure_camera", std::bind(&r_ws::_post_configure_camera, this, _1, _2, _3));
    _server.add_route(METHOD_POST, "/remove_camera", std::bind(&r_ws::_post_remove_camera, this, _1, _2, _3));
    _server.add_route(METHOD_POST, "/update_camera_properties", std::bind(&r_ws::_post_update_camera_properties, this, _1, _2, _3));
    _server.add_route(METHOD_POST, "/forget_camera", std::bind(&r_ws::_post_forget_camera, this, _1, _2, _3));
    _server.add_route(METHOD_POST, "/add_rtsp_camera", std::bind(&r_ws::_post_add_rtsp_camera, this, _1, _2, _3));

    _server.add_route(METHOD_GET, "/auth_status", std::bind(&r_ws::_get_auth_status, this, _1, _2, _3));
    _server.add_route(METHOD_POST, "/login", std::bind(&r_ws::_post_login, this, _1, _2, _3));
    _server.add_route(METHOD_POST, "/set_password", std::bind(&r_ws::_post_set_password, this, _1, _2, _3));

    // Cache the OS-protected master key once (used to encrypt/decrypt the system
    // password). Best-effort: if secure storage is unavailable the auth helpers
    // will fail closed (no token can be issued).
    try { _master_key = r_secure_store().get_master_key(); }
    catch(const exception& ex) { R_LOG_WARNING("r_ws: secure store unavailable, auth disabled: %s", ex.what()); }

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
            _server.serve_bundle("/", std::move(bundle));
            R_LOG_INFO("Web UI available at http://localhost:%d/", WEB_SERVER_PORT);
        }
        catch(const exception& ex)
        {
            R_LOG_WARNING("Failed to load web UI bundle (ui.rbt): %s", ex.what());
        }
    }

    _server.start();

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
    _server.stop();
}

r_http::r_server_response r_ws::_get_jpg(const r_http::r_web_server<r_utils::r_socket>&,
                                         r_utils::r_socket&,
                                         const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("camera_id") == end(args))
            R_THROW(("Missing camera_id."));

        if(args.find("start_time") == end(args))
            R_THROW(("Missing start_time."));

        uint16_t w = 640;
        if(args.find("width") != end(args))
            w = r_string_utils::s_to_uint16(args["width"]);

        uint16_t h = 480;
        if(args.find("height") != end(args))
            h = r_string_utils::s_to_uint16(args["height"]);

        auto result = query_get_jpg(
            _top_dir,
            _devices,
            args["camera_id"],
            r_time_utils::iso_8601_to_tp(args["start_time"]),
            w,
            h
        );

        r_server_response response;
        response.set_content_type("image/jpeg");
        response.set_body(result.size(), result.data());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to create jpg."));
}

r_http::r_server_response r_ws::_get_webp(const r_http::r_web_server<r_utils::r_socket>&,
                                          r_utils::r_socket&,
                                          const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("camera_id") == end(args))
            R_THROW(("Missing camera_id."));

        if(args.find("start_time") == end(args))
            R_THROW(("Missing start_time."));

        uint16_t w = 640;
        if(args.find("width") != end(args))
            w = r_string_utils::s_to_uint16(args["width"]);

        uint16_t h = 480;
        if(args.find("height") != end(args))
            h = r_string_utils::s_to_uint16(args["height"]);

        auto result = query_get_webp(
            _top_dir,
            _devices,
            args["camera_id"],
            r_time_utils::iso_8601_to_tp(args["start_time"]),
            w,
            h
        );

        r_server_response response;
        response.set_content_type("image/webp");
        response.set_body(result.size(), result.data());
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to create webp."));
}

r_http::r_server_response r_ws::_get_key_frame(const r_http::r_web_server<r_utils::r_socket>&,
                                               r_utils::r_socket&,
                                               const r_http::r_server_request& request)
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
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to fetch key frame."));
}

r_http::r_server_response r_ws::_get_contents(const r_http::r_web_server<r_utils::r_socket>&,
                                            r_utils::r_socket&,
                                            const r_http::r_server_request& request)
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
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to get contents."));
}

r_http::r_server_response r_ws::_get_cameras(const r_http::r_web_server<r_utils::r_socket>&,
                                           r_utils::r_socket&,
                                           const r_http::r_server_request& request)
{
    // Optional auth: if the client supplies a bearer token it must be valid.
    // Lets the web UI verify a stored token (and gate the page on it) while
    // still allowing unauthenticated API clients to read camera info.
    {
        auto auth_hdr = request.get_header("authorization");
        if(!auth_hdr.is_null() && !_token_valid(request))
            R_STHROW(r_http_401_exception, ("Unauthorized."));
    }

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
            auto res = _camera_source_resolution(_top_dir, c);

            bool receiving_video = false;
            bool stream_failed = false;
            auto sit = status_by_id.find(c.id);
            if(sit != status_by_id.end())
            {
                receiving_video = sit->second.receiving_video;
                stream_failed = sit->second.failed;
            }

            j["cameras"].push_back(
                {
                    {"id", c.id},
                    {"camera_name", (c.camera_name.is_null())?"":c.camera_name.value()},
                    {"friendly_name", (c.friendly_name.is_null())?"":c.friendly_name.value()},
                    {"ipv4", (c.ipv4.is_null())?"":c.ipv4.value()},
                    {"rtsp_url", (c.rtsp_url.is_null())?"":c.rtsp_url.value()},
                    {"video_codec", (c.video_codec.is_null())?"":c.video_codec.value()},
                    {"audio_codec", (c.audio_codec.is_null())?"":c.audio_codec.value()},
                    {"state", c.state},
                    {"do_motion_detection", do_motion_detection},
                    {"do_motion_pruning", (c.do_motion_pruning.is_null())?false:c.do_motion_pruning.value()},
                    {"min_continuous_recording_hours", (c.min_continuous_recording_hours.is_null())?24:c.min_continuous_recording_hours.value()},
                    {"record_file_path", (c.record_file_path.is_null())?"":c.record_file_path.value()},
                    {"receiving_video", receiving_video},
                    {"stream_failed", stream_failed},
                    {"manual", (c.xaddrs.is_null() && !c.rtsp_url.is_null())},
                    {"width", res.is_null() ? 0 : res.value().first},
                    {"height", res.is_null() ? 0 : res.value().second}
                }
            );
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

        try
        {
            if(job.type == export_type::standard)
                _export(job);
            else
                _transcode_export(job);
        }
        catch(const exception& ex)
        {
            R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
        }

        {
            lock_guard<mutex> lock(_export_progress_mutex);
            auto it = _export_progress.find(job_id);
            if(it != _export_progress.end())
            {
                it->second.percent_complete = 100;
                it->second.completed_at = steady_clock::now();
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

r_http::r_server_response r_ws::_get_export(const r_http::r_web_server<r_utils::r_socket>&,
                                            r_utils::r_socket&,
                                            const r_http::r_server_request& request)
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

                r_nullable<float> fr;

                auto parts = r_string_utils::split(video_codec_parameters, ",");
                for(auto part : parts)
                {
                    auto inner_parts = r_string_utils::split(part, "=");
                    if(inner_parts.size() == 2)
                    {
                        if(r_string_utils::strip(inner_parts[0]) == "sc_framerate")
                            fr.set_value(r_string_utils::s_to_float(inner_parts[1]));
                    }
                }

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

r_http::r_server_response r_ws::_get_analytics(const r_http::r_web_server<r_utils::r_socket>&,
                                               r_utils::r_socket&,
                                               const r_http::r_server_request& request)
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


r_http::r_server_response r_ws::_get_motion_events(const r_http::r_web_server<r_utils::r_socket>&,
                                                   r_utils::r_socket&,
                                                   const r_http::r_server_request& request)
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

r_http::r_server_response r_ws::_get_video(const r_http::r_web_server<r_utils::r_socket>&,
                                           r_utils::r_socket&,
                                           const r_http::r_server_request& request)
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
        if(it->second.last_used < oldest->second.last_used)
            oldest = it;
    }
    _sessions.erase(oldest);
}

r_http::r_server_response r_ws::_get_create_transcode_stream(const r_http::r_web_server<r_utils::r_socket>&,
                                                              r_utils::r_socket&,
                                                              const r_http::r_server_request& request)
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

        r_transcode_session session;
        session.output_width      = r_string_utils::s_to_uint16(args["width"]);
        session.output_height     = r_string_utils::s_to_uint16(args["height"]);
        session.bitrate           = r_string_utils::s_to_uint32(args["bitrate"]);
        session.framerate_num     = r_string_utils::s_to_uint32(args["framerate_num"]);
        session.framerate_den     = r_string_utils::s_to_uint32(args["framerate_den"]);
        session.output_codec_name = output_codec;
        session.codec_initialized = false;
        session.last_used         = steady_clock::now();

        if(args.find("audio_sample_rate") != args.end())
            session.target_audio_sample_rate = r_string_utils::s_to_int(args["audio_sample_rate"]);
        if(args.find("audio_channels") != args.end())
            session.target_audio_channels = r_string_utils::s_to_int(args["audio_channels"]);

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

r_http::r_server_response r_ws::_get_transcode(const r_http::r_web_server<r_utils::r_socket>&,
                                               r_utils::r_socket&,
                                               const r_http::r_server_request& request)
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

        lock_guard<mutex> lock(_sessions_mutex);

        auto it = _sessions.find(handle);
        if(it == _sessions.end())
            R_THROW(("Unknown transcode handle: %s", handle.c_str()));

        auto& session = it->second;
        session.last_used = steady_clock::now();

        // Fetch the raw video blobtree for the requested range.
        auto qr_buffer = query_get_video(_top_dir, _devices, args["camera_id"], start_tp, end_tp);

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

r_http::r_server_response r_ws::_get_finalize_transcode_stream(const r_http::r_web_server<r_utils::r_socket>&,
                                                                r_utils::r_socket&,
                                                                const r_http::r_server_request& request)
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

r_http::r_server_response r_ws::_get_transcode_export(const r_http::r_web_server<r_utils::r_socket>&,
                                                       r_utils::r_socket&,
                                                       const r_http::r_server_request& request)
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

r_http::r_server_response r_ws::_get_export_progress(const r_http::r_web_server<r_utils::r_socket>&,
                                                       r_utils::r_socket&,
                                                       const r_http::r_server_request& request)
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

r_http::r_server_response r_ws::_get_export_download(const r_http::r_web_server<r_utils::r_socket>&,
                                                       r_utils::r_socket&,
                                                       const r_http::r_server_request& request)
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

r_http::r_server_response r_ws::_get_camera_profiles(const r_http::r_web_server<r_utils::r_socket>&,
                                                     r_utils::r_socket&,
                                                     const r_http::r_server_request& request)
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

        auto profiles = _agent.get_camera_profiles(camera.ipv4.value(), camera.xaddrs.value(), username, password);

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

r_http::r_server_response r_ws::_get_measure_camera(const r_http::r_web_server<r_utils::r_socket>&,
                                                    r_utils::r_socket&,
                                                    const r_http::r_server_request& request)
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
            job.profile_token
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

r_http::r_server_response r_ws::_get_measure_progress(const r_http::r_web_server<r_utils::r_socket>&,
                                                      r_utils::r_socket&,
                                                      const r_http::r_server_request& request)
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

r_http::r_server_response r_ws::_get_measure_result(const r_http::r_web_server<r_utils::r_socket>&,
                                                    r_utils::r_socket&,
                                                    const r_http::r_server_request& request)
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

r_http::r_server_response r_ws::_post_configure_camera(const r_http::r_web_server<r_utils::r_socket>&,
                                                       r_utils::r_socket&,
                                                       const r_http::r_server_request& request)
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

r_http::r_server_response r_ws::_post_remove_camera(const r_http::r_web_server<r_utils::r_socket>&,
                                                    r_utils::r_socket&,
                                                    const r_http::r_server_request& request)
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

r_http::r_server_response r_ws::_post_update_camera_properties(const r_http::r_web_server<r_utils::r_socket>&,
                                                               r_utils::r_socket&,
                                                               const r_http::r_server_request& request)
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

        query_update_camera_properties(_top_dir, _devices, camera_id, do_motion, do_prune, min_hours);

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

r_http::r_server_response r_ws::_post_forget_camera(const r_http::r_web_server<r_utils::r_socket>&,
                                                    r_utils::r_socket&,
                                                    const r_http::r_server_request& request)
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

r_http::r_server_response r_ws::_post_add_rtsp_camera(const r_http::r_web_server<r_utils::r_socket>&,
                                                      r_utils::r_socket&,
                                                      const r_http::r_server_request& request)
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
        R_THROW(("Secure storage unavailable; cannot set system password."));
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
        R_THROW(("System password must not be empty."));
    _store_system_password(password);
    // Existing web sessions must re-authenticate against the new password.
    lock_guard<mutex> lock(_auth_mutex);
    _tokens.clear();
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
    lock_guard<mutex> lock(_auth_mutex);
    _tokens.clear();
}

// Constant-time string compare to avoid leaking the password via timing.
static bool _ct_equal(const std::string& a, const std::string& b)
{
    if(a.size() != b.size())
        return false;
    unsigned char acc = 0;
    for(size_t i = 0; i < a.size(); ++i)
        acc |= (unsigned char)(a[i] ^ b[i]);
    return acc == 0;
}

bool r_ws::_token_valid(const r_http::r_server_request& request)
{
    auto auth = request.get_header("authorization");
    if(auth.is_null())
        return false;

    const string prefix = "Bearer ";
    auto val = auth.value();
    if(val.size() <= prefix.size() || val.compare(0, prefix.size(), prefix) != 0)
        return false;
    auto token = val.substr(prefix.size());

    lock_guard<mutex> lock(_auth_mutex);
    auto it = _tokens.find(token);
    if(it == _tokens.end())
        return false;
    if(steady_clock::now() > it->second)
    {
        _tokens.erase(it);
        return false;
    }
    return true;
}

void r_ws::_require_auth(const r_http::r_server_request& request)
{
    if(!_token_valid(request))
        R_STHROW(r_http_401_exception, ("Unauthorized."));
}

r_http::r_server_response r_ws::_get_auth_status(const r_http::r_web_server<r_utils::r_socket>&,
                                                 r_utils::r_socket&,
                                                 const r_http::r_server_request&)
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

r_http::r_server_response r_ws::_post_login(const r_http::r_web_server<r_utils::r_socket>&,
                                            r_utils::r_socket&,
                                            const r_http::r_server_request& request)
{
    try
    {
        auto req = json::parse(request.get_body_as_string());
        if(!req.contains("password"))
            R_THROW(("Missing password."));

        auto stored = _load_system_password();
        if(stored.is_null())
            R_STHROW(r_http_401_exception, ("No system password set."));

        if(!_ct_equal(req["password"].get<string>(), stored.value()))
            R_STHROW(r_http_401_exception, ("Invalid password."));

        auto token = r_uuid::generate();
        {
            lock_guard<mutex> lock(_auth_mutex);
            _tokens[token] = steady_clock::now() + hours(24);
        }

        json j;
        j["token"] = token;

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const r_http_401_exception&) { throw; }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Login failed."));
}

r_http::r_server_response r_ws::_post_set_password(const r_http::r_web_server<r_utils::r_socket>&,
                                                   r_utils::r_socket&,
                                                   const r_http::r_server_request& request)
{
    try
    {
        auto req = json::parse(request.get_body_as_string());
        if(!req.contains("new_password"))
            R_THROW(("Missing new_password."));
        auto new_password = req["new_password"].get<string>();
        if(new_password.empty())
            R_THROW(("Password must not be empty."));

        // Bootstrap: the first password can be set without auth. Once a password
        // exists, changing it requires either a valid token or the current one.
        if(_system_password_set())
        {
            bool authorized = _token_valid(request);
            if(!authorized && req.contains("current_password"))
            {
                auto stored = _load_system_password();
                authorized = !stored.is_null() && _ct_equal(req["current_password"].get<string>(), stored.value());
            }
            if(!authorized)
                R_STHROW(r_http_401_exception, ("Provide a valid token or current_password to change the system password."));
        }

        _store_system_password(new_password);

        // Invalidate all outstanding tokens on a password change.
        {
            lock_guard<mutex> lock(_auth_mutex);
            _tokens.clear();
        }

        json j;
        j["status"] = "ok";

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        return response;
    }
    catch(const r_http_401_exception&) { throw; }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to set system password."));
}

r_server_response r_ws::_post_mcp(const r_web_server<r_socket>&,
                                   r_socket&,
                                   const r_server_request& request)
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
                {"serverInfo", {{"name", "revere"}, {"version", "1.0.0"}}}
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
