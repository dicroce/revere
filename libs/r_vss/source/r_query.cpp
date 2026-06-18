#include "r_vss/r_query.h"
#include "r_vss/r_motion_engine.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_file.h"
#include "r_utils/r_blob_tree.h"
#include "r_disco/r_camera.h"
#include "r_storage/r_storage_file.h"
#include "r_storage/r_storage_file_reader.h"
#include "r_storage/r_ring.h"
#include "r_storage/r_md_storage_file.h"
#include "r_pipeline/r_stream_info.h"
#include "r_pipeline/r_gst_source.h"
#include "r_av/r_video_decoder.h"
#include "r_av/r_video_encoder.h"
#include "r_av/r_muxer.h"
#include <functional>
#include <array>
#include <thread>
#include <tuple>

using namespace r_utils;
using namespace r_disco;
using namespace r_storage;
using namespace r_av;
using namespace r_vss;
using namespace std;
using namespace std::chrono;

// Helper to get storage file path - handles both legacy (filename only) and new (full path) formats
static string _get_storage_path(const string& record_file_path, const string& top_dir)
{
    // Check if it's already a full path (contains path separators)
    if(record_file_path.find('/') != string::npos || record_file_path.find('\\') != string::npos)
        return record_file_path;
    // Legacy format: just filename, prepend default video directory
    return top_dir + PATH_SLASH + "video" + PATH_SLASH + record_file_path;
}

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

// Helper: scale (src_w x src_h) to fit *inside* the (w x h) box while preserving
// aspect ratio, writing the fitted dimensions back into w/h. The result never
// exceeds the box on either axis and is never upscaled past the source's native
// resolution. Dimensions are rounded to even (4:2:0 encoders require it) and
// clamped to a sane minimum. A zero source or box dimension leaves w/h untouched.
static void _fit_inside(uint16_t src_w, uint16_t src_h, uint16_t& w, uint16_t& h)
{
    if(src_w == 0 || src_h == 0 || w == 0 || h == 0)
        return;

    double sx = (double)w / (double)src_w;
    double sy = (double)h / (double)src_h;
    double scale = (sx < sy) ? sx : sy;
    if(scale > 1.0) scale = 1.0;  // never upscale beyond native resolution

    int ow = (int)((double)src_w * scale + 0.5);
    int oh = (int)((double)src_h * scale + 0.5);
    ow &= ~1; oh &= ~1;           // even dimensions for 4:2:0 encoders
    if(ow < 2) ow = 2;
    if(oh < 2) oh = 2;

    w = (uint16_t)ow;
    h = (uint16_t)oh;
}

// Helper: decode a single frame with proper parser and flush support.
// When preserve_aspect is true, w/h are treated as a bounding box: the frame is
// scaled to fit inside while preserving aspect ratio, and w/h are overwritten
// with the actual output dimensions (so callers can size their encoder to match).
// When false, the frame is scaled to exactly w x h (legacy stretch behavior).
static shared_ptr<vector<uint8_t>> _decode_single_frame(
    const string& video_codec_name,
    const string& video_codec_parameters,
    const vector<uint8_t>& frame,
    AVPixelFormat output_format,
    uint16_t& w,
    uint16_t& h,
    bool preserve_aspect = false)
{
    // Enable parsing to properly handle Annex B streams with multiple NAL units
    auto codec_id = r_av::encoding_to_av_codec_id(video_codec_name);
    r_video_decoder decoder(codec_id, r_av::r_find_best_hw_accel(codec_id), true);

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
    {
        if(preserve_aspect)
            _fit_inside(decoder.input_width(), decoder.input_height(), w, h);
        return decoder.get(output_format, w, h, 1);
    }

    return nullptr;
}

vector<uint8_t> r_vss::query_get_jpg(const std::string& top_dir, r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(_get_storage_path(maybe_camera.value().record_file_path.value(), top_dir));

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

    // Treat w/h as a bounding box; _decode_single_frame rewrites them with the
    // aspect-preserved output dimensions, which the encoder must then match.
    auto decoded = _decode_single_frame(video_codec_name, video_codec_parameters, frame, AV_PIX_FMT_YUVJ420P, w, h, true);

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

// Reduce a friendly name to a filesystem-safe stem (no extension). Mirrors what
// the desktop wizard ends up with after the user types a name and we append .nts.
static string _safe_file_stem(const string& name)
{
    string out;
    for(char c : name)
    {
        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
            out.push_back(c);
        else if(c == ' ')
            out.push_back('_');
        // everything else (slashes, dots, colons, ...) is dropped
    }
    if(out.empty())
        out = "camera";
    return out;
}

// Allocate the motion ring buffer + analytics metadata storage alongside the
// recording file. Mirrors _create_motion_files() in the desktop app.
static void _create_motion_files(const string& motion_path)
{
    if(r_fs::file_exists(motion_path))
        return;
    r_ring::allocate(motion_path, 11, 2592000);          // 1 flag/sec, ~30 days
    r_md_storage_file::allocate(motion_path, 524288, 30); // 512KB blocks x 30
}

measured_camera r_vss::query_measure_camera(const string& top_dir, r_devices& devices, const string& camera_id, const r_nullable<string>& username, const r_nullable<string>& password, uint16_t w, uint16_t h)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    auto camera = maybe_camera.value();

    if(camera.rtsp_url.is_null())
        R_THROW(("Camera has not been interrogated (no rtsp_url)."));

    // Streams the camera for ~15s to measure its bitrate and grab a keyframe.
    auto cp = r_pipeline::fetch_camera_params(camera.rtsp_url.value(), username, password);

    if(cp.sdp_medias.empty() || cp.bytes_per_second == 0)
        R_THROW(("Unable to communicate with camera."));

    // Resolve the video codec params. An ONVIF camera has them stored by
    // interrogate_camera. A manually-added RTSP camera doesn't — but the SDP we
    // just pulled with fetch_camera_params has everything we need, so derive the
    // params from it and persist them (recording reads them too). This is the
    // RTSP equivalent of interrogate_camera, minus the ONVIF profile resolution.
    string video_codec, video_codec_params;
    if(!camera.video_codec.is_null() && !camera.video_codec_parameters.is_null())
    {
        video_codec = camera.video_codec.value();
        video_codec_params = camera.video_codec_parameters.value();
    }
    else
    {
        if(cp.sdp_medias.find("video") == cp.sdp_medias.end())
            R_THROW(("Camera stream has no video media."));

        int video_timebase = 0;
        std::tie(video_codec, video_codec_params, video_timebase) =
            r_pipeline::sdp_media_map_to_s(r_pipeline::VIDEO_MEDIA, cp.sdp_medias);

        camera.video_codec.set_value(video_codec);
        camera.video_codec_parameters.set_value(video_codec_params);
        camera.video_timebase.set_value(video_timebase);

        if(cp.sdp_medias.find("audio") != cp.sdp_medias.end())
        {
            string acodec, aparams; int atb = 0;
            std::tie(acodec, aparams, atb) =
                r_pipeline::sdp_media_map_to_s(r_pipeline::AUDIO_MEDIA, cp.sdp_medias);
            camera.audio_codec.set_value(acodec);
            camera.audio_codec_parameters.set_value(aparams);
            camera.audio_timebase.set_value(atb);
        }

        devices.save_camera(camera);
    }

    measured_camera out;
    out.byte_rate = cp.bytes_per_second;
    out.video_codec = video_codec;

    // Decode the freshly-grabbed keyframe and MJPEG-encode it as a snapshot for
    // the friendly-name dialog. Handles both inline and out-of-band SPS/PPS.
    // Best effort: a decode failure just yields no snapshot, not a failure.
    if(!video_codec.empty() && !video_codec_params.empty() && !cp.video_key_frame.empty())
    {
        auto decoded = _decode_single_frame(video_codec, video_codec_params, cp.video_key_frame, AV_PIX_FMT_YUVJ420P, w, h);
        if(decoded)
        {
            r_video_encoder encoder(AV_CODEC_ID_MJPEG, 100000, w, h, {1,1}, AV_PIX_FMT_YUVJ420P, 0, 1, 0, 0);
            encoder.attach_buffer(decoded->data(), decoded->size(), 0);
            auto es = encoder.encode();
            if(es == R_CODEC_STATE_HAS_OUTPUT)
            {
                auto pi = encoder.get();
                out.jpeg.assign(pi.data, pi.data + pi.size);
            }
        }
    }

    return out;
}

void r_vss::query_configure_camera(const string& top_dir, r_devices& devices, const string& camera_id, const r_nullable<string>& username, const r_nullable<string>& password, const string& friendly_name, bool do_motion_detection, double retention_days, int64_t byte_rate)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    auto camera = maybe_camera.value();

    if(camera.rtsp_url.is_null())
        R_THROW(("Camera has not been interrogated (no rtsp_url)."));

    auto video_path = top_dir + PATH_SLASH + "video";
    if(!r_fs::file_exists(video_path))
        r_fs::mkdir(video_path);

    // Derive a unique .nts filename from the friendly name.
    auto stem = _safe_file_stem(friendly_name.empty() ? camera_id : friendly_name);
    auto file_name = stem + ".nts";
    auto storage_path = video_path + PATH_SLASH + file_name;
    for(int n = 1; r_fs::file_exists(storage_path); ++n)
    {
        file_name = stem + "_" + to_string(n) + ".nts";
        storage_path = video_path + PATH_SLASH + file_name;
    }

    // Size the ring buffer from the measured bitrate and requested retention.
    auto sz = r_storage::required_file_size_for_retention_hours((int64_t)(retention_days * 24.0), byte_rate);
    int64_t num_blocks = sz.first;
    int64_t block_size = sz.second;

    uint64_t fs_size = 0, fs_free = 0;
    r_fs::get_fs_usage(video_path, fs_size, fs_free);
    if(fs_free < (uint64_t)(block_size * num_blocks))
        R_THROW(("Not enough free space on storage device."));

    string motion_path;
    if(do_motion_detection)
    {
        auto dot = file_name.find_last_of('.');
        auto motion_file_name = (dot == string::npos) ? (file_name + ".mdb") : (file_name.substr(0, dot) + ".mdb");
        motion_path = video_path + PATH_SLASH + motion_file_name;
        _create_motion_files(motion_path);
    }

    r_storage_file::allocate(storage_path, block_size, num_blocks);

    // Populate the camera row and flip it to "assigned"; the stream keeper's
    // poll loop (~2s) notices the new assignment and starts recording.
    // Only overwrite credentials when supplied — a manually-added camera keeps
    // the credentials captured in the Add dialog (get_camera_by_id returns them
    // decrypted; assign_camera re-encrypts on save).
    if(!username.is_null())
        camera.rtsp_username = username;
    if(!password.is_null())
        camera.rtsp_password = password;
    camera.friendly_name = friendly_name;
    camera.record_file_path = storage_path;
    camera.n_record_file_blocks = num_blocks;
    camera.record_file_block_size = block_size;
    camera.do_motion_detection.set_value(do_motion_detection);
    if(do_motion_detection)
        camera.motion_detection_file_path.set_value(motion_path);

    devices.assign_camera(camera);
}

// Best-effort deletion of a camera's storage files. Mirrors _delete_camera_files
// in the desktop app: the main .nts plus the motion ring (.mdb), its sqlite
// sidecar (.db), and the analytics metadata store (.mdnts/.mdnts.db and its
// WAL/SHM journals). Each removal is independent — a missing or locked file is
// logged and skipped, never fatal.
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

static void _delete_camera_files(const string& top_dir, const r_camera& camera)
{
    if(!camera.record_file_path.is_null())
        _remove_if_exists(_get_storage_path(camera.record_file_path.value(), top_dir));

    if(!camera.motion_detection_file_path.is_null())
    {
        auto mdb = _get_storage_path(camera.motion_detection_file_path.value(), top_dir);
        auto base = (mdb.size() >= 4 && mdb.substr(mdb.size() - 4) == ".mdb")
                    ? mdb.substr(0, mdb.size() - 4)
                    : mdb;
        _remove_if_exists(base + ".mdb");            // motion ring buffer
        _remove_if_exists(base + ".db");             // ring buffer sqlite sidecar
        _remove_if_exists(base + ".mdnts");          // analytics metadata store
        _remove_if_exists(base + ".mdnts.db");       // metadata sqlite
        _remove_if_exists(base + ".mdnts.db-shm");   // WAL shared-memory
        _remove_if_exists(base + ".mdnts.db-wal");   // WAL journal
    }
}

void r_vss::query_remove_camera(const string& top_dir, r_devices& devices, const string& camera_id, bool delete_files)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    auto camera = maybe_camera.value();

    // Unassign first: the stream keeper's poll loop (~2s) notices the camera
    // left the assigned set, tears down its recording context and releases all
    // file handles.
    devices.unassign_camera(camera);

    if(delete_files)
    {
        // Give the stream keeper time to close handles before we delete (Windows
        // won't remove a file with open handles). Matches the desktop's wait.
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        _delete_camera_files(top_dir, camera);
    }
}

void r_vss::query_update_camera_properties(const string& top_dir, r_devices& devices, const string& camera_id, bool do_motion_detection, bool do_motion_pruning, int min_continuous_recording_hours)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    auto camera = maybe_camera.value();

    // When motion detection is on, make sure the motion files exist — a camera
    // first recorded with motion off won't have them yet. Derive the path from
    // the recording file (.nts -> .mdb), mirroring the desktop.
    if(do_motion_detection)
    {
        string motion_file_name;
        if(!camera.motion_detection_file_path.is_null())
            motion_file_name = camera.motion_detection_file_path.value();

        auto existing = motion_file_name.empty() ? string() : _get_storage_path(motion_file_name, top_dir);
        if(motion_file_name.empty() || !r_fs::file_exists(existing))
        {
            if(camera.record_file_path.is_null())
                R_THROW(("Camera has no recording file to derive a motion path from."));
            auto base = camera.record_file_path.value();
            auto dot = base.find_last_of('.');
            motion_file_name = (dot == string::npos) ? (base + ".mdb") : (base.substr(0, dot) + ".mdb");
            _create_motion_files(_get_storage_path(motion_file_name, top_dir));
        }
        camera.motion_detection_file_path.set_value(motion_file_name);
    }

    camera.do_motion_detection.set_value(do_motion_detection);
    camera.do_motion_pruning.set_value(do_motion_pruning);
    camera.min_continuous_recording_hours.set_value(min_continuous_recording_hours);

    devices.save_camera(camera);
}

vector<uint8_t> r_vss::query_get_webp(const string& top_dir, r_devices& devices, const string& camera_id, chrono::system_clock::time_point ts, uint16_t w, uint16_t h)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(_get_storage_path(maybe_camera.value().record_file_path.value(), top_dir));

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

    // Treat w/h as a bounding box; _decode_single_frame rewrites them with the
    // aspect-preserved output dimensions, which the encoder must then match.
    auto decoded = _decode_single_frame(video_codec_name, video_codec_parameters, frame, AV_PIX_FMT_YUV420P, w, h, true);

    if(decoded)
    {
        r_video_encoder encoder(AV_CODEC_ID_WEBP, 100000, w, h, {1,1}, AV_PIX_FMT_YUV420P, 0, 1, 0, 0);
        encoder.attach_buffer(decoded->data(), decoded->size(), 0);
        auto es = encoder.encode();
        if(es != R_CODEC_STATE_HAS_OUTPUT)
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

    r_storage_file_reader sf(_get_storage_path(maybe_camera.value().record_file_path.value(), top_dir));

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

    r_storage_file_reader sf(_get_storage_path(maybe_camera.value().record_file_path.value(), top_dir));

    return sf.query_key(R_STORAGE_MEDIA_TYPE_VIDEO, r_time_utils::tp_to_epoch_millis(ts));
}

vector<uint8_t> r_vss::query_get_bgr24_frame(const std::string& top_dir, r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point ts, uint16_t w, uint16_t h)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sf(_get_storage_path(maybe_camera.value().record_file_path.value(), top_dir));

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

    r_storage_file_reader sf(_get_storage_path(maybe_camera.value().record_file_path.value(), top_dir));

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

    r_storage_file_reader sf(_get_storage_path(maybe_camera.value().record_file_path.value(), top_dir));

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

    r_storage_file_reader sf(_get_storage_path(maybe_camera.value().record_file_path.value(), top_dir));

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

    auto raw_first = sf.first_ts();
    if(!raw_first.is_null())
        c.first_ts.set_value(r_time_utils::epoch_millis_to_tp(raw_first.value()));

    auto raw_last = sf.last_ts();
    if(!raw_last.is_null())
        c.last_ts.set_value(r_time_utils::epoch_millis_to_tp(raw_last.value()));

    return c;
}

r_nullable<system_clock::time_point> r_vss::query_get_first_ts(const std::string& top_dir, r_devices& devices, const std::string& camera_id)
{
    auto maybe_camera = devices.get_camera_by_id(camera_id);
    if(maybe_camera.is_null())
        R_THROW(("Unknown camera id: %s", camera_id.c_str()));

    if(maybe_camera.value().record_file_path.is_null())
        R_THROW(("Camera has no recording file!"));

    r_storage_file_reader sfr(_get_storage_path(maybe_camera.value().record_file_path.value(), top_dir));

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

vector<r_vss::motion_event_info> r_vss::query_get_motion_events(const std::string& top_dir, r_devices& devices, const std::string& camera_id, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end)
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

        // Handle both full path and legacy filename format for motion files
        auto motion_path = _get_storage_path(motion_file_name, top_dir);

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

                if((current_second - event_start_second) >= 2)
                {
                    motion_event_info mi;
                    mi.start = system_clock::time_point(seconds(event_start_second));
                    mi.end = system_clock::time_point(seconds(current_second));
                    mi.motion = 0;      // Dummy value for now
                    mi.avg_motion = 0;  // Dummy value for now
                    mi.stddev = 0;      // Dummy value for now

                    result.push_back(mi);
                }
            }
        }

        // Handle case where event extends to end of query range
        if(in_event)
        {
            int64_t end_second = start_seconds + (int64_t)motion_data.size();
            if((end_second - event_start_second) >= 2)
            {
                motion_event_info mi;
                mi.start = system_clock::time_point(seconds(event_start_second));
                mi.end = system_clock::time_point(seconds(end_second));
                mi.motion = 0;      // Dummy value for now
                mi.avg_motion = 0;  // Dummy value for now
                mi.stddev = 0;      // Dummy value for now

                result.push_back(mi);
            }
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

    r_storage_file_reader sf(_get_storage_path(maybe_camera.value().record_file_path.value(), top_dir));

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
    
    auto file_name = _get_storage_path(maybe_camera.value().record_file_path.value(), top_dir);

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
    
    // Get the full storage path (handles both legacy and new formats)
    auto storage_path = _get_storage_path(record_file_path, top_dir);
    
    // Remove .nts extension if present and add .mdnts
    std::string full_path = storage_path;
    if(full_path.size() > 4 && full_path.substr(full_path.size() - 4) == ".nts")
    {
        full_path = full_path.substr(0, full_path.size() - 4);
    }
    full_path += ".mdnts";

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