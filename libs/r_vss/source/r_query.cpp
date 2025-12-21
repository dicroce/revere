#include "r_vss/r_query.h"
#include "r_vss/r_motion_engine.h"
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
using namespace std;
using namespace std::chrono;

// Helper: check if frame data has inline SPS (H.264 NAL type 7)
static bool _has_inline_sps(const vector<uint8_t>& frame)
{
    for(size_t i = 0; i + 4 < frame.size() && i < 500; ++i)
    {
        if(frame[i] == 0x00 && frame[i+1] == 0x00 &&
           frame[i+2] == 0x00 && frame[i+3] == 0x01)
        {
            uint8_t nal_type = frame[i+4] & 0x1F;
            if(nal_type == 7) // SPS
                return true;
        }
    }
    return false;
}

// Helper: decode a single frame with proper parser and flush support
static shared_ptr<vector<uint8_t>> _decode_single_frame(
    const string& video_codec_name,
    const string& video_codec_parameters,
    const vector<uint8_t>& frame,
    AVPixelFormat output_format,
    uint16_t w,
    uint16_t h)
{
    // Enable parsing to properly handle Annex B streams with multiple NAL units
    r_video_decoder decoder(r_av::encoding_to_av_codec_id(video_codec_name), true);

    // Only set extradata if stream doesn't have inline SPS/PPS
    if(!_has_inline_sps(frame))
        decoder.set_extradata(r_pipeline::get_video_codec_extradata(video_codec_name, video_codec_parameters));

    decoder.attach_buffer(frame.data(), frame.size());

    int attempts = 0;
    auto ds = decoder.decode();
    while(ds != R_CODEC_STATE_HAS_OUTPUT && ds != R_CODEC_STATE_AGAIN_HAS_OUTPUT && attempts < 10)
    {
        ds = decoder.decode();
        ++attempts;
    }

    // If decoder accepted data but hasn't produced output yet, flush to force output
    if(ds == R_CODEC_STATE_HUNGRY)
    {
        while(ds != R_CODEC_STATE_HAS_OUTPUT && ds != R_CODEC_STATE_AGAIN_HAS_OUTPUT && attempts < 20)
        {
            ds = decoder.flush();
            ++attempts;
        }
    }

    if(ds == R_CODEC_STATE_HAS_OUTPUT || ds == R_CODEC_STATE_AGAIN_HAS_OUTPUT)
        return decoder.get(output_format, w, h, 1);

    return nullptr;
}

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

    auto decoded = _decode_single_frame(video_codec_name, video_codec_parameters, frame, AV_PIX_FMT_YUVJ420P, w, h);

    if(decoded)
    {
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

    auto decoded = _decode_single_frame(video_codec_name, video_codec_parameters, frame, AV_PIX_FMT_YUV420P, w, h);

    if(decoded)
    {
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

    auto decoded = _decode_single_frame(video_codec_name, video_codec_parameters, frame, AV_PIX_FMT_BGR24, w, h);

    if(decoded)
    {
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

    auto decoded = _decode_single_frame(video_codec_name, video_codec_parameters, frame, AV_PIX_FMT_RGB24, w, h);

    if(decoded)
    {
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

        r_ring r(motion_path, RING_MOTION_FLAG_SIZE);

        vector<uint8_t> motion_data = r.query_raw(start, end);

        // Each byte is a motion flag: 1 = motion, 0 = no motion
        // Find contiguous runs of 1s and create events from those

        int64_t start_seconds = duration_cast<seconds>(start.time_since_epoch()).count();

        bool in_event = false;
        int64_t event_start_second = 0;

        for(size_t i = 0; i < motion_data.size(); ++i)
        {
            bool has_motion = (motion_data[i] != 0);
            int64_t current_second = start_seconds + (int64_t)i;

            if(has_motion && !in_event)
            {
                // Start of a new event
                in_event = true;
                event_start_second = current_second;
            }
            else if(!has_motion && in_event)
            {
                // End of current event
                in_event = false;

                motion_event_info mi;
                mi.start = system_clock::time_point(seconds(event_start_second));
                mi.end = system_clock::time_point(seconds(current_second));
                mi.motion = 0;      // Dummy value for now
                mi.avg_motion = 0;  // Dummy value for now
                mi.stddev = 0;      // Dummy value for now

                result.push_back(mi);
            }
        }

        // Handle case where event extends to end of query range
        if(in_event)
        {
            motion_event_info mi;
            mi.start = system_clock::time_point(seconds(event_start_second));
            mi.end = system_clock::time_point(seconds(start_seconds + (int64_t)motion_data.size()));
            mi.motion = 0;      // Dummy value for now
            mi.avg_motion = 0;  // Dummy value for now
            mi.stddev = 0;      // Dummy value for now

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