
#include "r_vss/r_ws.h"
#include "r_vss/r_vss_utils.h"
#include "r_vss/r_motion_engine.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_file.h"
#include "r_utils/r_blob_tree.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_disco/r_camera.h"
#include "r_storage/r_storage_file.h"
#include "r_storage/r_storage_file_reader.h"
#include "r_storage/r_ring.h"
#include "r_pipeline/r_stream_info.h"
#include "r_mux/r_muxer.h"
#include "r_codec/r_video_decoder.h"
#include "r_codec/r_video_encoder.h"
#include <functional>
#include <array>

using namespace r_utils;
using namespace r_http;
using namespace r_disco;
using namespace r_storage;
using namespace r_mux;
using namespace r_codec;
using namespace r_vss;
using namespace std;
using namespace std::chrono;
using namespace std::placeholders;
using json = nlohmann::json;

const int WEB_SERVER_PORT = 10080;

struct motion_info
{
    int64_t time;
    uint8_t motion;
    uint8_t avg_motion;
    uint8_t stddev;
};

r_ws::r_ws(const string& top_dir, r_devices& devices) :
    _top_dir(top_dir),
    _devices(devices),
    _server(WEB_SERVER_PORT)
{
    _server.add_route(METHOD_GET, "/jpg", std::bind(&r_ws::_get_jpg, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/contents", std::bind(&r_ws::_get_contents, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/cameras", std::bind(&r_ws::_get_cameras, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/export", std::bind(&r_ws::_get_export, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/motions", std::bind(&r_ws::_get_motions, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/motion_events", std::bind(&r_ws::_get_motion_events, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/key_frame", std::bind(&r_ws::_get_key_frame, this, _1, _2, _3));

    _server.start();
}

r_ws::~r_ws()
{
    _server.stop();
}

vector<uint8_t> r_ws::get_jpg(const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h)
{
    auto maybe_camera = _devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(_top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    auto key_bt = sf.query_key(R_STORAGE_MEDIA_TYPE_VIDEO, r_time_utils::tp_to_epoch_millis(ts));

    uint32_t version = 0;
    auto bt = r_blob_tree::deserialize(&key_bt[0], key_bt.size(), version);

    auto video_codec_name = bt["video_codec_name"].get_string();
    auto video_codec_parameters = bt["video_codec_parameters"].get_string();

    if(bt["frames"].size() != 1)
        R_THROW(("Expected exactly one frame in blob tree."));

    if(!bt["frames"][0].has_key("key"))
        R_THROW(("Expected frame to have key."));

    auto key = (bt["frames"][0]["key"].get_string() == "true");

    if(!bt["frames"][0].has_key("data"))
        R_THROW(("Expected frame to have data."));

    auto frame = bt["frames"][0]["data"].get();

    r_video_decoder decoder(r_mux::encoding_to_av_codec_id(video_codec_name));
    decoder.set_extradata(r_pipeline::get_video_codec_extradata(video_codec_name, video_codec_parameters));
    decoder.attach_buffer(frame.data(), frame.size());
    auto ds = decoder.decode();
    if(ds == R_CODEC_STATE_HAS_OUTPUT)
    {
        auto decoded = decoder.get(AV_PIX_FMT_YUV420P, w, h);

        r_video_encoder encoder(AV_CODEC_ID_MJPEG, 100000, w, h, {1,1}, AV_PIX_FMT_YUV420P, 0, 1, 0, 0);
        encoder.attach_buffer(decoded->data(), decoded->size());
        auto es = encoder.encode();
        if(es == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto pi = encoder.get();

            vector<uint8_t> result(pi.size);
            memcpy(result.data(), pi.data, pi.size);
            return result;
        }
    }

    R_THROW(("Unable to JPG fail."));
}

chrono::hours r_ws::get_retention_hours(const std::string& camera_id)
{
    auto maybe_camera = _devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(_top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    auto maybe_first_ts = sf.first_ts();

    if(maybe_first_ts.is_null())
        return chrono::hours(0);

    return chrono::duration_cast<chrono::hours>(chrono::system_clock::now() - r_time_utils::epoch_millis_to_tp(maybe_first_ts.value()));
}

vector<uint8_t> r_ws::get_key_frame(const std::string& camera_id, std::chrono::system_clock::time_point ts)
{
    auto maybe_camera = _devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(_top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    return sf.query_key(R_STORAGE_MEDIA_TYPE_VIDEO, r_time_utils::tp_to_epoch_millis(ts));
}

vector<uint8_t> r_ws::get_video(const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end)
{
    auto maybe_camera = _devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(_top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    return sf.query(
        R_STORAGE_MEDIA_TYPE_ALL,
        chrono::duration_cast<std::chrono::milliseconds>(start.time_since_epoch()).count(),
        chrono::duration_cast<std::chrono::milliseconds>(end.time_since_epoch()).count()
    );
}

contents r_ws::get_contents(const string& camera_id, system_clock::time_point start, system_clock::time_point end)
{
    auto maybe_camera = _devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(_top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    auto segments = sf.query_segments(
        r_time_utils::tp_to_epoch_millis(start),
        r_time_utils::tp_to_epoch_millis(end)
    );

    vector<segment> result;

    for(auto& s : segments)
    {
        segment seg;
        seg.start = r_time_utils::epoch_millis_to_tp(s.first);
        seg.end = r_time_utils::epoch_millis_to_tp(s.second);

        result.push_back(seg);
    }

    contents c;
    c.segments = result;
    c.first_ts = r_time_utils::epoch_millis_to_tp(sf.first_ts());
    c.last_ts = r_time_utils::epoch_millis_to_tp(sf.last_ts());

    return c;
}

r_nullable<system_clock::time_point> r_ws::get_first_ts(const std::string& camera_id)
{
    auto maybe_camera = _devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sfr(_top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    r_nullable<system_clock::time_point> result;

    auto first_ts = sfr.first_ts();
    if(!first_ts.is_null())
        result = r_time_utils::epoch_millis_to_tp(first_ts.value());

    return result;
}

vector<r_camera> r_ws::get_cameras()
{
    return _devices.get_all_cameras();
}

vector<motion_event_info> r_ws::get_motion_events(const std::string& camera_id, uint8_t motion_threshold, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end)
{
    auto maybe_camera = _devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().motion_detection_file_path.is_null())
        R_THROW(("Camera has no motion recording file!"));

    auto motion_file_name = maybe_camera.value().motion_detection_file_path.value();

    auto motion_path = _top_dir + PATH_SLASH + "video" + PATH_SLASH + motion_file_name;

    if(!r_fs::file_exists(motion_path))
        R_THROW(("Motion database file does not exist."));

    r_ring r(motion_path, RING_MOTION_EVENT_SIZE);

    vector<uint8_t> motion_data = r.query_raw(start, end);

    // OK, so we have a bunch of raw motion data... We need to make events. Here are some

    // 1. Create a map<> of motion event time to motion data (motion, avg_motion and stddev).
    map<int64_t, motion_info> motion_infos;
    for(int i = 0; i < motion_data.size() / RING_MOTION_EVENT_SIZE; ++i)
    {
        motion_info mi;
        mi.time = *(int64_t*)(&motion_data[(i*RING_MOTION_EVENT_SIZE)]);
        mi.motion = motion_data[(i*RING_MOTION_EVENT_SIZE)+8];
        mi.avg_motion = motion_data[(i*RING_MOTION_EVENT_SIZE)+9];
        mi.stddev = motion_data[(i*RING_MOTION_EVENT_SIZE)+10];
        if(mi.motion >= motion_threshold)    
            motion_infos.insert(make_pair(mi.time, mi));
    }

    // 2. Create a vector<> of motion times.
    vector<int64_t> motion_times;
    for(auto& mi : motion_infos)
        motion_times.push_back(mi.first);

    // 3. Call r_vss::find_contiguous_segments() to find events.
    auto events = r_vss::find_contiguous_segments(motion_times);

    vector<motion_event_info> result;

    for(auto e : events)
    {
        // for each event, the motion, stddev and avg_motion will be taken from the motion data with the largest
        // motion value. Effectively the event becomes the highwater mark of the raw motions.
        auto first = motion_infos.find(e.first);
        auto last = motion_infos.find(e.second);

        auto max = max_element(first, last, [](const auto& a, const auto& b){ return a.second.motion < b.second.motion; });

        motion_event_info mi;

        mi.start = system_clock::time_point(milliseconds(e.first));
        mi.end = system_clock::time_point(milliseconds(e.second));
        mi.motion = max->second.motion;
        mi.avg_motion = max->second.avg_motion;
        mi.stddev = max->second.stddev;

        result.push_back(mi);        
    }

    return result;
}

// Note: r_storage_file_reader::query_blocks() returns a vector of pairs of timestamps. Note that the dumbdex only has block start
// times, so the block end times returned here are the start times of the next block. This makes these segments particularly suited for
// calling remove_blocks().

vector<segment> r_ws::get_blocks(const string& camera_id, system_clock::time_point start, system_clock::time_point end)
{
    auto maybe_camera = _devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(_top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    vector<pair<int64_t, int64_t>> blocks;

    if(start == system_clock::time_point())
        blocks = sf.query_blocks();
    else blocks = sf.query_blocks(
        r_time_utils::tp_to_epoch_millis(start),
        r_time_utils::tp_to_epoch_millis(end)
    );

    vector<segment> results;
    for(auto b : blocks)
    {
        segment s;
        s.start = r_time_utils::epoch_millis_to_tp(b.first);
        s.end = r_time_utils::epoch_millis_to_tp(b.second);
        results.push_back(s);
    }

    return results;
}

void r_ws::remove_blocks(const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end)
{
    auto maybe_camera = _devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    // Note: the false here is very important. We do not want to fix live segments here.
    r_storage_file sf(_top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value(), false);

    sf.remove_blocks(r_time_utils::tp_to_epoch_millis(start), r_time_utils::tp_to_epoch_millis(end));
}

r_http::r_server_response r_ws::_get_jpg(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                         r_utils::r_buffered_socket<r_utils::r_socket>& conn,
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

        auto result = get_jpg(
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

r_http::r_server_response r_ws::_get_key_frame(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                               r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                               const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        if(args.find("camera_id") == end(args))
            R_THROW(("Missing camera_id."));

        if(args.find("start_time") == end(args))
            R_THROW(("Missing start_time."));

        auto result = get_key_frame(
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

r_http::r_server_response r_ws::_get_contents(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                            r_utils::r_buffered_socket<r_utils::r_socket>& conn,
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

        auto contents = get_contents(
            args["camera_id"],
            r_time_utils::iso_8601_to_tp(start_time_s),
            r_time_utils::iso_8601_to_tp(end_time_s)            
        );

        json j;
        j["segments"] = json::array();

        for(auto& s : contents.segments)
        {
            // note: instead of push_back here its also possible to use += operator to append json object to array
            j["segments"].push_back({{"start_time", r_time_utils::tp_to_iso_8601(s.start, input_z_time)},
                                    {"end_time", r_time_utils::tp_to_iso_8601(s.end, input_z_time)}});

        }

        j["first_ts"] = r_time_utils::tp_to_iso_8601(contents.first_ts, input_z_time);
        j["last_ts"] = r_time_utils::tp_to_iso_8601(contents.last_ts, input_z_time);

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

r_http::r_server_response r_ws::_get_cameras(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                           r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                           const r_http::r_server_request& request)
{
    try
    {
        auto cameras = get_cameras();

        json j;
        j["cameras"] = json::array();

        for(auto c : cameras)
        {
            bool do_motion_detection = (c.do_motion_detection.is_null())?false:c.do_motion_detection.value();

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
                    {"do_motion_detection", do_motion_detection}
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

r_http::r_server_response r_ws::_get_export(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                            r_utils::r_buffered_socket<r_utils::r_socket>& conn,
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

        auto maybe_camera = _devices.get_camera_by_id(args["camera_id"]);

        if(maybe_camera.is_null())
            R_THROW(("Unknown camera id: %s", args["camera_id"].c_str()));

        if(maybe_camera.value().record_file_path.is_null())
            R_THROW(("Camera has no recording file!"));

        r_storage_file_reader sf(_top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

        if(args.find("start_time") == args.end())
            R_THROW(("Missing start_time."));

        auto start_time_s = args["start_time"];

        if(args.find("end_time") == args.end())
            R_THROW(("Missing end_time."));
        
        auto end_time_s = args["end_time"];

        if(args.find("file_name") == args.end())
            R_THROW(("Missing file name."));

        r_muxer muxer(exports_path + PATH_SLASH + args["file_name"]);

        auto qs = r_time_utils::iso_8601_to_tp(start_time_s);

        auto qe = r_time_utils::iso_8601_to_tp(end_time_s);

        bool muxer_opened = false;

        int64_t ts_first_frame = 0;

        bool done = false;

        while(!done)
        {
            auto rs = qs;
            auto re = rs;
            if(rs + std::chrono::minutes(5) >= qe)
            {
                re = qe;
                done = true;
            }
            else re = rs + std::chrono::minutes(5);

            auto qr_buffer = sf.query(
                R_STORAGE_MEDIA_TYPE_ALL,
                duration_cast<std::chrono::milliseconds>(rs.time_since_epoch()).count(),
                duration_cast<std::chrono::milliseconds>(re.time_since_epoch()).count()
            );

            qs = re;

            uint32_t version = 0;
            auto bt = r_blob_tree::deserialize(qr_buffer.data(), qr_buffer.size(), version);

            _check_timestamps(bt);

            if(!muxer_opened)
            {
                muxer_opened = true;

                // first extract metadata from the blob tree

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

                // now, look for the sc_framerate or estimate it...

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

                // get the video codec information and add the right kinds of video stream...

                auto video_codec_id = r_mux::encoding_to_av_codec_id(video_codec_name);

                if(video_codec_id == AV_CODEC_ID_H264)
                {
                    auto maybe_sps = r_pipeline::get_h264_sps(video_codec_parameters);
                    if(!maybe_sps.is_null())
                    {
                        auto sps_info = r_pipeline::parse_h264_sps(maybe_sps.value());

                        muxer.add_video_stream(
                            av_d2q(fr, 10000),
                            video_codec_id,
                            sps_info.width,
                            sps_info.height,
                            sps_info.profile_idc,
                            sps_info.level_idc
                        );
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

                        muxer.add_video_stream(
                            av_d2q(fr, 10000),
                            video_codec_id,
                            sps_info.width,
                            sps_info.height,
                            sps_info.profile_idc,
                            sps_info.level_idc
                        );

                        //muxer.set_video_bitstream_filter("hevc_mp4toannexb");
                    }
                    auto maybe_pps = r_pipeline::get_h265_pps(video_codec_parameters);
                    muxer.set_video_extradata(r_pipeline::make_h265_extradata(maybe_vps, maybe_sps, maybe_pps));
                }

                // If there is audio, add the audio stream...

                if(has_audio)
                {
                    r_nullable<int> audio_rate, audio_channels;
                    auto parts = r_string_utils::split(audio_codec_parameters, ",");
                    for(auto part : parts)
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

                    auto audio_codec_id = r_mux::encoding_to_av_codec_id(audio_codec_name);

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

                    muxer.add_audio_stream(
                        audio_codec_id,
                        audio_channels.value(),
                        audio_rate.value()
                    );
                }

                muxer.open();
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

                auto frame = bt["frames"][fi]["data"].get();

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

        muxer.finalize();

        r_server_response response;
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }

    R_STHROW(r_http_500_exception, ("Failed to export."));
}

r_http::r_server_response r_ws::_get_motions(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                             r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                             const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        auto maybe_camera = _devices.get_camera_by_id(args["camera_id"]);

        if(maybe_camera.is_null())
            R_THROW(("Unknown camera id: %s", args["camera_id"].c_str()));

        if(maybe_camera.value().motion_detection_file_path.is_null())
            R_THROW(("Camera has no motion recording file!"));

        uint8_t motion_threshold = 0;
        if(args.count("motion_threshold") > 0)
            motion_threshold = r_string_utils::s_to_uint8(args["motion_threshold"]);

        auto motion_file_name = maybe_camera.value().motion_detection_file_path.value();

        auto motion_path = _top_dir + PATH_SLASH + "video" + PATH_SLASH + motion_file_name;

        if(!r_fs::file_exists(motion_path))
            R_THROW(("Motion database file does not exist."));

        r_ring r(motion_path, RING_MOTION_EVENT_SIZE);

        if(args.find("start_time") == args.end())
            R_THROW(("Missing start_time."));

        auto start_time_s = args["start_time"];

        auto start_tp = r_time_utils::iso_8601_to_tp(start_time_s);

        bool input_z_time = start_time_s.find("Z") != std::string::npos;

        if(args.find("end_time") == args.end())
            R_THROW(("Missing end_time."));
        
        auto end_time_s = args["end_time"];

        auto end_tp = r_time_utils::iso_8601_to_tp(end_time_s);

        vector<uint8_t> motion_data = r.query_raw(start_tp, end_tp);

        json j;
        j["motions"] = json::array();

        for(int i = 0; i < motion_data.size() / RING_MOTION_EVENT_SIZE; ++i)
        {
            uint8_t motion_value = motion_data[(i*RING_MOTION_EVENT_SIZE)+8];

            if(motion_value >= motion_threshold)
            {
                json j_motion;
                int64_t motion_ts_millis = *(int64_t*)(motion_data.data() + (i*RING_MOTION_EVENT_SIZE));
                time_point<system_clock, milliseconds> tp{milliseconds{motion_ts_millis}};
                j_motion["time"] = r_time_utils::tp_to_iso_8601(tp, input_z_time);

                j_motion["motion"] = motion_value;
                j_motion["avg_motion"] = motion_data[(i*RING_MOTION_EVENT_SIZE)+9];
                j_motion["stddev"] = motion_data[(i*RING_MOTION_EVENT_SIZE)+10];

                j["motions"].push_back(j_motion);
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

    R_STHROW(r_http_500_exception, ("Failed to query motions."));
}


r_http::r_server_response r_ws::_get_motion_events(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                                   r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                                   const r_http::r_server_request& request)
{
    try
    {
        auto args = request.get_uri().get_get_args();

        auto maybe_camera = _devices.get_camera_by_id(args["camera_id"]);

        if(maybe_camera.is_null())
            R_THROW(("Unknown camera id: %s", args["camera_id"].c_str()));

        if(maybe_camera.value().motion_detection_file_path.is_null())
            R_THROW(("Camera has no motion recording file!"));

        uint8_t motion_threshold = 1;
        if(args.count("motion_threshold") > 0)
            motion_threshold = r_string_utils::s_to_uint8(args["motion_threshold"]);

        auto motion_file_name = maybe_camera.value().motion_detection_file_path.value();

        auto motion_path = _top_dir + PATH_SLASH + "video" + PATH_SLASH + motion_file_name;

        if(!r_fs::file_exists(motion_path))
            R_THROW(("Motion database file does not exist."));

        r_ring r(motion_path, RING_MOTION_EVENT_SIZE);

        if(args.find("start_time") == args.end())
            R_THROW(("Missing start_time."));

        auto start_time_s = args["start_time"];

        auto start_tp = r_time_utils::iso_8601_to_tp(start_time_s);

        bool input_z_time = start_time_s.find("Z") != std::string::npos;

        if(args.find("end_time") == args.end())
            R_THROW(("Missing end_time."));
        
        auto end_time_s = args["end_time"];

        auto end_tp = r_time_utils::iso_8601_to_tp(end_time_s);

        auto motion_events = get_motion_events(maybe_camera.value().id, motion_threshold, start_tp, end_tp);

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
