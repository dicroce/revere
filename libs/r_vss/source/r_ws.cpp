
#include "r_vss/r_ws.h"
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

const int WEB_SERVER_PORT = 10080;

r_ws::r_ws(const string& top_dir, r_devices& devices) :
    _top_dir(top_dir),
    _devices(devices),
    _server(WEB_SERVER_PORT)
{
    _server.add_route(METHOD_GET, "/jpg", std::bind(&r_ws::_get_jpg, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/webp", std::bind(&r_ws::_get_webp, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/contents", std::bind(&r_ws::_get_contents, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/cameras", std::bind(&r_ws::_get_cameras, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/export", std::bind(&r_ws::_get_export, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/motion_events", std::bind(&r_ws::_get_motion_events, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/key_frame", std::bind(&r_ws::_get_key_frame, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/analytics", std::bind(&r_ws::_get_analytics, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/video", std::bind(&r_ws::_get_video, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/create_transcode_stream", std::bind(&r_ws::_get_create_transcode_stream, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/transcode", std::bind(&r_ws::_get_transcode, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/finalize_transcode_stream", std::bind(&r_ws::_get_finalize_transcode_stream, this, _1, _2, _3));
    _server.add_route(METHOD_GET, "/transcode_export", std::bind(&r_ws::_get_transcode_export, this, _1, _2, _3));

    _server.start();
}

r_ws::~r_ws()
{
    _server.stop();
}

void r_ws::stop()
{
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
        j["segments"] = json::array();

        for(auto& s : contents.segments)
        {
            // note: instead of push_back here its also possible to use += operator to append json object to array
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
                                           const r_http::r_server_request&)
{
    try
    {
        auto cameras = query_get_cameras(_devices);

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

        auto start_time_s = args["start_time"];

        if(args.find("end_time") == args.end())
            R_THROW(("Missing end_time."));
        
        auto end_time_s = args["end_time"];

        if(args.find("file_name") == args.end())
            R_THROW(("Missing file name."));

        r_muxer muxer(exports_path + PATH_SLASH + args["file_name"]);
        muxer.enable_faststart();

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

            auto qr_buffer = query_get_video(
                _top_dir,
                _devices,
                args["camera_id"],
                rs,
                re
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

                auto video_codec_id = r_av::encoding_to_av_codec_id(video_codec_name);

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

                    muxer.add_audio_stream(
                        audio_codec_id,
                        (uint8_t)audio_channels.value(),
                        (uint16_t)audio_rate.value()
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

        uint8_t motion_threshold = 1;
        if(args.count("motion_threshold") > 0)
            motion_threshold = r_string_utils::s_to_uint8(args["motion_threshold"]);

        if(args.find("start_time") == args.end())
            R_THROW(("Missing start_time."));

        auto start_time_s = args["start_time"];

        auto start_tp = r_time_utils::iso_8601_to_tp(start_time_s);

        bool input_z_time = start_time_s.find("Z") != std::string::npos;

        if(args.find("end_time") == args.end())
            R_THROW(("Missing end_time."));
        
        auto end_time_s = args["end_time"];

        auto end_tp = r_time_utils::iso_8601_to_tp(end_time_s);

        auto motion_events = query_get_motion_events(_top_dir, _devices, args["camera_id"], motion_threshold, start_tp, end_tp);

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

            session.decoder = make_unique<r_video_decoder>(codec_id);
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
                level
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
                // Use a monotonic frame counter as the encoder PTS rather than the
                // raw millisecond wall-clock timestamp.  With time_base {1, fps}, a
                // counter of 0,1,2,... means one frame per time unit (correct).
                // Passing ms timestamps (e.g. 1706000000033) would make libx264 think
                // consecutive frames are ~1 second apart, breaking rate control.
                session.encoder->attach_buffer(decoded->data(), decoded->size(), session.encoder_frame_count++);

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
            }
            else if(sid == R_STORAGE_MEDIA_TYPE_AUDIO && session.audio_initialized && ts >= start_ms)
            {
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

                        session.audio_encoder->attach_buffer(enc_buf.data(), enc_buf.size(), session.audio_pts);
                        session.audio_pts += frame_size;

                        auto enc_state = session.audio_encoder->encode();
                        if(enc_state == R_CODEC_STATE_HAS_OUTPUT)
                        {
                            auto pi = session.audio_encoder->get();
                            out_bt["frames"][out_frame_idx]["stream_id"] = to_string((int)R_STORAGE_MEDIA_TYPE_AUDIO);
                            out_bt["frames"][out_frame_idx]["ts"]        = to_string(ts);
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

        string output_codec = "h264";
        if(args.find("codec") != args.end())
            output_codec = r_string_utils::to_lower(args["codec"]);

        auto output_width    = r_string_utils::s_to_uint16(args["width"]);
        auto output_height   = r_string_utils::s_to_uint16(args["height"]);
        auto bitrate         = r_string_utils::s_to_uint32(args["bitrate"]);
        auto framerate_num   = r_string_utils::s_to_uint32(args["framerate_num"]);
        auto framerate_den   = r_string_utils::s_to_uint32(args["framerate_den"]);

        AVRational framerate{(int)framerate_num, (int)framerate_den};

        auto qs = r_time_utils::iso_8601_to_tp(args["start_time"]);
        auto qe = r_time_utils::iso_8601_to_tp(args["end_time"]);

        r_muxer muxer(exports_path + PATH_SLASH + args["file_name"]);
        muxer.enable_faststart();

        bool initialized = false;
        bool has_audio = false;
        int64_t ts_first_frame = 0;
        int64_t encoder_frame_count = 0; // monotonic counter; avoids rounding when converting ms→frames

        unique_ptr<r_video_decoder> decoder;
        unique_ptr<r_video_encoder> encoder;

        bool done = false;
        while(!done)
        {
            auto rs = qs;
            auto re = rs;
            if(rs + chrono::minutes(5) >= qe)
            {
                re = qe;
                done = true;
            }
            else re = rs + chrono::minutes(5);

            auto qr_buffer = query_get_video(_top_dir, _devices, args["camera_id"], rs, re);
            qs = re;

            uint32_t version = 0;
            auto bt = r_blob_tree::deserialize(qr_buffer.data(), qr_buffer.size(), version);

            if(!initialized)
            {
                initialized = true;

                if(!bt.has_key("video_codec_name"))
                    R_THROW(("Blob tree missing video_codec_name."));
                if(!bt.has_key("video_codec_parameters"))
                    R_THROW(("Blob tree missing video_codec_parameters."));

                auto in_codec_name   = bt["video_codec_name"].get_string();
                auto in_codec_params = bt["video_codec_parameters"].get_string();

                has_audio = bt.has_key("has_audio") && (bt["has_audio"].get_string() == "true");

                // Build decoder from input stream codec.
                auto in_codec_id  = r_av::encoding_to_av_codec_id(in_codec_name);
                auto in_extradata = r_pipeline::get_video_codec_extradata(in_codec_name, in_codec_params);

                decoder = make_unique<r_video_decoder>(in_codec_id);
                if(!in_extradata.empty())
                    decoder->set_extradata(in_extradata);

                // Build encoder targeting the requested output codec.
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
                    level
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
            }

            if(!bt.has_key("frames"))
                R_THROW(("Blob tree missing frames."));

            size_t n_frames = bt["frames"].size();
            for(size_t fi = 0; fi < n_frames; ++fi)
            {
                if(!bt["frames"].has_index(fi)) continue;

                auto sid       = bt["frames"][fi]["stream_id"].get_value<int>();
                auto ts        = bt["frames"][fi]["ts"].get_value<int64_t>();
                auto frame_data = bt["frames"][fi]["data"].get_blob(); // copy: muxer needs non-const ptr

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

        // Drain any frames still buffered in the encoder.
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

        r_server_response response;
        return response;
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to transcode export."));
}
