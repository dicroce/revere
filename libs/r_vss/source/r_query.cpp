#include "r_vss/r_query.h"
#include "r_vss/r_vss_utils.h"
#include "r_vss/r_motion_engine.h"
#include "r_motion/utils.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_file.h"
#include "r_utils/r_blob_tree.h"
#include "r_disco/r_camera.h"
#include "r_storage/r_storage_file.h"
#include "r_storage/r_storage_file_reader.h"
#include "r_storage/r_ring.h"
#include "r_pipeline/r_stream_info.h"
#include "r_av/r_video_decoder.h"
#include "r_av/r_video_encoder.h"
#include "r_av/r_muxer.h"
#include <functional>
#include <array>

using namespace r_utils;
using namespace r_disco;
using namespace r_storage;
using namespace r_av;
using namespace r_vss;
using namespace r_motion;
using namespace std;
using namespace std::chrono;

vector<uint8_t> r_vss::query_get_jpg(const std::string& top_dir, r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    auto epoch_millis = r_time_utils::tp_to_epoch_millis(ts);

    auto key_bt = sf.query_key(R_STORAGE_MEDIA_TYPE_VIDEO, epoch_millis);

    uint32_t version = 0;
    auto bt = r_blob_tree::deserialize(&key_bt[0], key_bt.size(), version);

    auto video_codec_name = bt["video_codec_name"].get_string();
    auto video_codec_parameters = bt["video_codec_parameters"].get_string();

    if(bt["frames"].size() != 1)
        R_THROW(("Expected exactly one frame in blob tree."));

    if(!bt["frames"][0].has_key("data"))
        R_THROW(("Expected frame to have data."));

    auto frame = bt["frames"][0]["data"].get_blob();

    r_video_decoder decoder(r_av::encoding_to_av_codec_id(video_codec_name));
    decoder.set_extradata(r_pipeline::get_video_codec_extradata(video_codec_name, video_codec_parameters));
    decoder.attach_buffer(frame.data(), frame.size());
    auto ds = decoder.decode();

    int attempts = 10;
    while(ds != R_CODEC_STATE_HAS_OUTPUT && attempts > 0)
    {
        ds = decoder.decode();
        attempts--;
    }

    if(ds == R_CODEC_STATE_HAS_OUTPUT)
    {
        auto decoded = decoder.get(AV_PIX_FMT_YUVJ420P, w, h, 1);

        r_video_encoder encoder(AV_CODEC_ID_MJPEG, 100000, w, h, {1,1}, AV_PIX_FMT_YUVJ420P, 0, 1, 0, 0);
        encoder.attach_buffer(decoded->data(), decoded->size(), 0);
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

vector<uint8_t> r_vss::query_get_webp(const string& top_dir, r_devices& devices, const string& camera_id, chrono::system_clock::time_point ts, uint16_t w, uint16_t h)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    auto epoch_millis = r_time_utils::tp_to_epoch_millis(ts);

    auto key_bt = sf.query_key(R_STORAGE_MEDIA_TYPE_VIDEO, epoch_millis);

    uint32_t version = 0;
    auto bt = r_blob_tree::deserialize(&key_bt[0], key_bt.size(), version);

    auto video_codec_name = bt["video_codec_name"].get_string();
    auto video_codec_parameters = bt["video_codec_parameters"].get_string();

    if(bt["frames"].size() != 1)
        R_THROW(("Expected exactly one frame in blob tree."));

    if(!bt["frames"][0].has_key("data"))
        R_THROW(("Expected frame to have data."));

    auto frame = bt["frames"][0]["data"].get_blob();

    r_video_decoder decoder(r_av::encoding_to_av_codec_id(video_codec_name));
    decoder.set_extradata(r_pipeline::get_video_codec_extradata(video_codec_name, video_codec_parameters));
    decoder.attach_buffer(frame.data(), frame.size());
    auto ds = decoder.decode();

    int attempts = 10;
    while(ds != R_CODEC_STATE_HAS_OUTPUT && attempts > 0)
    {
        ds = decoder.decode();
        attempts--;
    }

    if(ds == R_CODEC_STATE_HAS_OUTPUT)
    {
        auto decoded = decoder.get(AV_PIX_FMT_YUV420P, w, h, 1);

        r_video_encoder encoder(AV_CODEC_ID_WEBP, 100000, w, h, {1,1}, AV_PIX_FMT_YUV420P, 0, 1, 0, 0);
        encoder.attach_buffer(decoded->data(), decoded->size(), 0);
        auto es = encoder.encode();

        es = encoder.flush();

        if(es == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto pi = encoder.get();

            vector<uint8_t> result(pi.size);
            memcpy(result.data(), pi.data, pi.size);
            return result;
        }
    }

    R_THROW(("Unable to webp fail."));
}

chrono::hours r_vss::query_get_retention_hours(const std::string& top_dir, r_devices& devices, const std::string& camera_id)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    auto maybe_first_ts = sf.first_ts();

    if(maybe_first_ts.is_null())
        return chrono::hours(0);

    return chrono::duration_cast<chrono::hours>(chrono::system_clock::now() - r_time_utils::epoch_millis_to_tp(maybe_first_ts.value()));
}

vector<uint8_t> r_vss::query_get_key_frame(const std::string& top_dir, r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    return sf.query_key(R_STORAGE_MEDIA_TYPE_VIDEO, r_time_utils::tp_to_epoch_millis(ts));
}

vector<uint8_t> r_vss::query_get_bgr24_frame(const std::string& top_dir, r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    auto key_bt = sf.query_key(R_STORAGE_MEDIA_TYPE_VIDEO, r_time_utils::tp_to_epoch_millis(ts));

    uint32_t version = 0;
    auto bt = r_blob_tree::deserialize(&key_bt[0], key_bt.size(), version);

    auto video_codec_name = bt["video_codec_name"].get_string();
    auto video_codec_parameters = bt["video_codec_parameters"].get_string();

    if(bt["frames"].size() != 1)
        R_THROW(("Expected exactly one frame in blob tree."));

    if(!bt["frames"][0].has_key("data"))
        R_THROW(("Expected frame to have data."));

    auto frame = bt["frames"][0]["data"].get_blob();

    r_video_decoder decoder(r_av::encoding_to_av_codec_id(video_codec_name));
    decoder.set_extradata(r_pipeline::get_video_codec_extradata(video_codec_name, video_codec_parameters));
    decoder.attach_buffer(frame.data(), frame.size());
    auto ds = decoder.decode();

    int attempts = 10;
    while(ds != R_CODEC_STATE_HAS_OUTPUT && attempts > 0)
    {
        ds = decoder.decode();
        attempts--;
    }

    if(ds == R_CODEC_STATE_HAS_OUTPUT)
    {
        auto decoded = decoder.get(AV_PIX_FMT_BGR24, w, h, 1);
        
        vector<uint8_t> result(decoded->size());
        memcpy(result.data(), decoded->data(), decoded->size());
        return result;
    }

    R_THROW(("Unable to decode frame."));
}

vector<uint8_t> r_vss::query_get_rgb24_frame(const std::string& top_dir, r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    auto key_bt = sf.query_key(R_STORAGE_MEDIA_TYPE_VIDEO, r_time_utils::tp_to_epoch_millis(ts));

    uint32_t version = 0;
    auto bt = r_blob_tree::deserialize(&key_bt[0], key_bt.size(), version);

    auto video_codec_name = bt["video_codec_name"].get_string();
    auto video_codec_parameters = bt["video_codec_parameters"].get_string();

    if(bt["frames"].size() != 1)
        R_THROW(("Expected exactly one frame in blob tree."));

    if(!bt["frames"][0].has_key("data"))
        R_THROW(("Expected frame to have data."));

    auto frame = bt["frames"][0]["data"].get_blob();

    r_video_decoder decoder(r_av::encoding_to_av_codec_id(video_codec_name));
    decoder.set_extradata(r_pipeline::get_video_codec_extradata(video_codec_name, video_codec_parameters));
    decoder.attach_buffer(frame.data(), frame.size());
    auto ds = decoder.decode();

    int attempts = 10;
    while(ds != R_CODEC_STATE_HAS_OUTPUT && attempts > 0)
    {
        ds = decoder.decode();
        attempts--;
    }

    if(ds == R_CODEC_STATE_HAS_OUTPUT)
    {
        auto decoded = decoder.get(AV_PIX_FMT_RGB24, w, h, 1);
        
        vector<uint8_t> result(decoded->size());
        memcpy(result.data(), decoded->data(), decoded->size());
        return result;
    }

    R_THROW(("Unable to decode frame."));
}

vector<uint8_t> r_vss::query_get_video(const std::string& top_dir, r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    return sf.query(
        R_STORAGE_MEDIA_TYPE_ALL,
        chrono::duration_cast<std::chrono::milliseconds>(start.time_since_epoch()).count(),
        chrono::duration_cast<std::chrono::milliseconds>(end.time_since_epoch()).count()
    );
}

contents r_vss::query_get_contents(const string& top_dir, r_devices& devices, const string& camera_id, system_clock::time_point start, system_clock::time_point end)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

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

    return c;
}

r_nullable<system_clock::time_point> r_vss::query_get_first_ts(const std::string& top_dir, r_devices& devices, const std::string& camera_id)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sfr(top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

    r_nullable<system_clock::time_point> result;

    auto first_ts = sfr.first_ts();
    if(!first_ts.is_null())
        result = r_time_utils::epoch_millis_to_tp(first_ts.value());

    return result;
}

vector<r_camera> r_vss::query_get_cameras(r_devices& devices)
{
    return devices.get_all_cameras();
}

vector<r_vss::motion_event_info> r_vss::query_get_motion_events(const std::string& top_dir, r_devices& devices, const std::string& camera_id, uint8_t motion_threshold, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end)
{
    vector<motion_event_info> result;

    try
    {
    
        auto maybe_camera = devices.get_camera_by_id(camera_id);

        if(maybe_camera.is_null())
            R_THROW(("Unknown camera id: %s", camera_id.c_str()));

        if(maybe_camera.value().motion_detection_file_path.is_null())
            R_THROW(("Camera has no motion recording file!"));

        auto motion_file_name = maybe_camera.value().motion_detection_file_path.value();

        auto motion_path = top_dir + PATH_SLASH + "video" + PATH_SLASH + motion_file_name;

        if(!r_fs::file_exists(motion_path))
            R_THROW(("Motion database file does not exist."));

        r_ring r(motion_path, RING_MOTION_EVENT_SIZE);

        vector<uint8_t> motion_data = r.query_raw(start, end);

        // OK, so we have a bunch of raw motion data... We need to make events. Here are some

        struct motion_info
        {
            int64_t time;
            uint8_t motion;
            uint8_t avg_motion;
            uint8_t stddev;
        };

        // 1. Create a map<> of motion event time to motion data (motion, avg_motion and stddev).
        map<int64_t, motion_info> motion_infos;
        for(int i = 0; i < (int)(motion_data.size() / RING_MOTION_EVENT_SIZE); ++i)
        {
            motion_info mi;
            mi.time = *(int64_t*)(&motion_data[(i*RING_MOTION_EVENT_SIZE)]);
            mi.motion = motion_data[(i*RING_MOTION_EVENT_SIZE)+8];
            mi.avg_motion = motion_data[(i*RING_MOTION_EVENT_SIZE)+9];
            mi.stddev = motion_data[(i*RING_MOTION_EVENT_SIZE)+10];
            // Use statistical significance test like the motion engine does
            if(is_motion_significant(mi.motion, mi.avg_motion, mi.stddev))    
                motion_infos.insert(make_pair(mi.time, mi));
        }

        // 2. Create a vector<> of motion times.
        vector<int64_t> motion_times;
        for(auto& mi : motion_infos)
            motion_times.push_back(mi.first);

        // 3. Call r_vss::find_contiguous_segments() to find events.
        auto events = r_vss::find_contiguous_segments(motion_times);

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
    }
    catch(const std::exception& e)
    {
        R_LOG_ERROR("Error getting motion events: %s", e.what());
    }

    return result;
}

vector<r_vss::segment> r_vss::query_get_blocks(const string& top_dir, r_devices& devices, const string& camera_id, system_clock::time_point start, system_clock::time_point end)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value());

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

void r_vss::query_remove_blocks(const std::string& top_dir, r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));
    
    auto file_name = top_dir + PATH_SLASH + "video" + PATH_SLASH + maybe_camera.value().record_file_path.value();

    r_storage_file::remove_blocks(file_name, r_time_utils::tp_to_epoch_millis(start), r_time_utils::tp_to_epoch_millis(end));
}

vector<r_vss::motion_data_point> r_vss::query_get_motions(const std::string& top_dir, r_devices& devices, const std::string& camera_id, uint8_t motion_threshold, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end)
{
    vector<motion_data_point> result;

    auto maybe_camera = devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().motion_detection_file_path.is_null())
        R_THROW(("Camera has no motion recording file!"));

    auto motion_file_name = maybe_camera.value().motion_detection_file_path.value();

    auto motion_path = top_dir + PATH_SLASH + "video" + PATH_SLASH + motion_file_name;

    if(!r_fs::file_exists(motion_path))
        R_THROW(("Motion database file does not exist."));

    r_ring r(motion_path, RING_MOTION_EVENT_SIZE);

    vector<uint8_t> motion_data = r.query_raw(start, end);

    for(int i = 0; i < (int)(motion_data.size() / RING_MOTION_EVENT_SIZE); ++i)
    {
        uint8_t motion_value = motion_data[(i*RING_MOTION_EVENT_SIZE)+8];
        uint8_t avg_motion = motion_data[(i*RING_MOTION_EVENT_SIZE)+9];
        uint8_t stddev = motion_data[(i*RING_MOTION_EVENT_SIZE)+10];

        // Use statistical significance test like the motion engine does
        if(is_motion_significant(motion_value, avg_motion, stddev))
        {
            motion_data_point mdp;
            int64_t motion_ts_millis = *(int64_t*)(motion_data.data() + (i*RING_MOTION_EVENT_SIZE));
            mdp.time = system_clock::time_point(milliseconds{motion_ts_millis});
            mdp.motion = motion_value;
            mdp.avg_motion = avg_motion;
            mdp.stddev = stddev;

            result.push_back(mdp);
        }
    }

    return result;
}

vector<r_metadata_entry> r_vss::query_get_analytics(const string& top_dir, r_devices& devices, const string& camera_id, system_clock::time_point start, system_clock::time_point end, const r_nullable<string>& stream_tag)
{
    vector<r_metadata_entry> result;

    auto maybe_camera = devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    // Construct metadata file path (same as recording file but with .mdnts extension)
    auto record_file_path = maybe_camera.value().record_file_path.value();
    
    // Remove .nts extension if present
    std::string base_path = record_file_path;
    if(base_path.size() > 4 && base_path.substr(base_path.size() - 4) == ".nts")
    {
        base_path = base_path.substr(0, base_path.size() - 4);
    }
    
    auto mdnts_file_path = base_path + ".mdnts";
    auto full_path = top_dir + PATH_SLASH + "video" + PATH_SLASH + mdnts_file_path;

    // Check if metadata file exists
    if(!r_fs::file_exists(full_path))
    {
        // Return empty result if no metadata file exists (not an error)
        return result;
    }

    try
    {
        // Open the metadata file reader
        r_md_storage_file_reader reader(full_path);

        // Query metadata entries within the time range
        int64_t start_ms = r_time_utils::tp_to_epoch_millis(start);
        int64_t end_ms = r_time_utils::tp_to_epoch_millis(end);
        
        if(!stream_tag.is_null())
        {
            // Query specific stream tag
            result = reader.query_stream(stream_tag.value(), start_ms, end_ms);
        }
        else
        {
            // Query all streams
            result = reader.query_all(start_ms, end_ms);
        }
    }
    catch(const exception& e)
    {
        R_LOG_ERROR("Error reading analytics data: %s", e.what());
    }

    return result;
}