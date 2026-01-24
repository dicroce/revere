
#include "r_vss/r_motion_engine.h"
#include "r_vss/r_motion_event.h"
#include "r_vss/r_vss_utils.h"
#include "r_motion/utils.h"
#include "r_av/r_muxer.h"
#include "r_av/r_codec_state.h"
#include "r_pipeline/r_stream_info.h"
#include "r_pipeline/r_gst_buffer.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include <opencv2/opencv.hpp>
#include <chrono>
#include <algorithm>
#include <utility>

using namespace r_vss;
using namespace r_utils;
using namespace r_storage;
using namespace r_motion;
using namespace std;
using namespace std::chrono;

namespace {
    // Helper to get storage file path - handles both legacy (filename only) and new (full path) formats
    string _get_storage_path(const string& file_path, const string& top_dir)
    {
        // Check if it's already a full path (contains path separators)
        if(file_path.find('/') != string::npos || file_path.find('\\') != string::npos)
            return file_path;
        // Legacy format: just filename, prepend default video directory
        return top_dir + PATH_SLASH + "video" + PATH_SLASH + file_path;
    }

    // Letterbox parameters for 640x640 target
    struct letterbox_params {
        int scaled_w;
        int scaled_h;
        int pad_x;
        int pad_y;
        float scale;
    };

    letterbox_params calc_letterbox(int input_w, int input_h, int target_size = 640) {
        letterbox_params p;
        p.scale = std::min((float)target_size / input_w, (float)target_size / input_h);
        p.scaled_w = (int)(input_w * p.scale);
        p.scaled_h = (int)(input_h * p.scale);
        p.pad_x = (target_size - p.scaled_w) / 2;
        p.pad_y = (target_size - p.scaled_h) / 2;
        return p;
    }

    // Create letterboxed 640x640 image and return ROI mat for motion detection
    // letterbox_out: receives the full 640x640 letterboxed image
    // Returns: ROI mat pointing to content region (zero-copy)
    cv::Mat create_letterbox(const std::vector<uint8_t>& decoded_data, int scaled_w, int scaled_h,
                             const letterbox_params& lp, cv::Mat& letterbox_out) {
        // Wrap decoded data as cv::Mat (no copy)
        cv::Mat content(scaled_h, scaled_w, CV_8UC3, const_cast<uint8_t*>(decoded_data.data()));

        // Create 640x640 black image
        letterbox_out = cv::Mat::zeros(640, 640, CV_8UC3);

        // Copy content to center region
        cv::Rect roi(lp.pad_x, lp.pad_y, lp.scaled_w, lp.scaled_h);
        content.copyTo(letterbox_out(roi));

        // Return ROI mat (zero-copy reference to content region in letterbox)
        return letterbox_out(roi);
    }
}

r_motion_engine::r_motion_engine(r_disco::r_devices& devices, const string& top_dir, r_motion_event_plugin_host& meph) :
    _devices(devices),
    _top_dir(top_dir),
    _work(),
    _work_contexts(),
    _running(false),
    _thread(),
    _meph(meph)
{
}

r_motion_engine::~r_motion_engine() noexcept
{
    stop();
}

void r_motion_engine::start()
{
    _thread = thread(&r_motion_engine::_entry_point, this);
}

void r_motion_engine::stop() noexcept
{
    if(_running)
    {
        _running = false;
        _work.wake();
        _thread.join();
    }
}

void r_motion_engine::post_frame(r_pipeline::r_gst_buffer buffer, int64_t ts, const string& video_codec_name, const string& video_codec_parameters, const string& id, bool is_key_frame)
{
    r_work_item item;
    item.frame = buffer;
    item.video_codec_name = video_codec_name;
    item.video_codec_parameters = video_codec_parameters;
    item.id = id;
    item.ts = ts;
    item.is_key_frame = is_key_frame;

    _work.post(item);
}

void r_motion_engine::remove_work_context(const string& camera_id)
{
    // Post a special work item to trigger removal in the worker thread
    // This ensures the work context is removed from the same thread that accesses it
    r_work_item item;
    item.id = camera_id;
    item.ts = -1; // Use -1 as a sentinel value to indicate removal request
    _work.post(item);
}

size_t r_motion_engine::get_and_reset_dropped_count()
{
    size_t count = _work.dropped_count();
    _work.reset_dropped_count();
    return count;
}

size_t r_motion_engine::get_queue_size() const
{
    return _work.size();
}

void r_motion_engine::_entry_point()
{
    _running = true;

    while(_running)
    {
        auto maybe_work = _work.poll(chrono::milliseconds(1000));

        if(!maybe_work.is_null())
        {
            auto work = maybe_work.value();

            // Check for removal request (sentinel value ts == -1)
            if(work.ts == -1)
            {
                auto found_wc = _work_contexts.find(work.id);
                if(found_wc != _work_contexts.end())
                {
                    _work_contexts.erase(found_wc);
                }
                continue;
            }

            try
            {
                auto found_wc = _work_contexts.find(work.id);
                if(found_wc == _work_contexts.end())
                    found_wc = _create_work_context(work);

                auto& wc = found_wc->second;

                if(work.is_key_frame)
                {
                    auto mi = work.frame.map(r_pipeline::r_gst_buffer::MT_READ);
                    int max_decode_attempts = 10;
                    wc->decoder().attach_buffer(mi.data(), mi.size());

                    bool decode_again = true;
                    while(decode_again)
                    {
                        if(max_decode_attempts <= 0)
                            R_THROW(("Unable to decode!"));
                        --max_decode_attempts;

                        auto ds = wc->decoder().decode();

                        if(ds == r_av::R_CODEC_STATE_HAS_OUTPUT || ds == r_av::R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                        {
                            uint16_t input_w = wc->decoder().input_width();
                            uint16_t input_h = wc->decoder().input_height();

                            // Calculate letterbox parameters for 640x640 target
                            auto lp = calc_letterbox(input_w, input_h);

                            // Decode to scaled size (maintains aspect ratio)
                            auto decoded = wc->decoder().get(AV_PIX_FMT_RGB24, (uint16_t)lp.scaled_w, (uint16_t)lp.scaled_h, 1);

                            // Create 640x640 letterboxed image and get ROI for motion detection
                            cv::Mat letterbox_img;
                            cv::Mat roi_mat = create_letterbox(*decoded, lp.scaled_w, lp.scaled_h, lp, letterbox_img);

                            // Process motion on ROI only (efficient), with offset correction
                            auto maybe_motion_info = wc->motion_state().process(roi_mat, lp.pad_x, lp.pad_y, false);

                            if(!maybe_motion_info.is_null())
                            {
                                auto motion_info = maybe_motion_info.value();
                                bool is_significant = is_motion_significant(motion_info.motion, motion_info.avg_motion, motion_info.stddev);

                                // Convert motion region from r_motion to r_vss format
                                // Coordinates are already in 640x640 letterbox space (corrected by motion_state)
                                r_vss::motion_region motion_bbox;
                                motion_bbox.x = motion_info.motion_bbox.x;
                                motion_bbox.y = motion_info.motion_bbox.y;
                                motion_bbox.width = motion_info.motion_bbox.width;
                                motion_bbox.height = motion_info.motion_bbox.height;
                                motion_bbox.has_motion = motion_info.motion_bbox.has_motion;

                                // Copy letterboxed image to vector
                                std::vector<uint8_t> letterbox_data(letterbox_img.data,
                                    letterbox_img.data + letterbox_img.total() * letterbox_img.elemSize());

                                // Push to keyframe motion buffer for event start detection
                                r_keyframe_motion_entry kf_entry;
                                kf_entry.ts = work.ts;
                                kf_entry.has_motion = is_significant;
                                kf_entry.decoded_image = letterbox_data;
                                kf_entry.width = 640;
                                kf_entry.height = 640;
                                kf_entry.bbox = motion_bbox;
                                wc->keyframe_motion_buffer().push(kf_entry);

                                // Event state machine (keyframe-only mode)
                                if(!wc->get_in_event())
                                {
                                    // Check if we should start an event (N consecutive keyframes with motion AND sufficient displacement)
                                    size_t n = wc->get_motion_confirm_frames();
                                    double min_disp = wc->get_min_motion_displacement();

                                    // Lambda to extract bbox center from keyframe entry
                                    auto get_bbox_center = [](const r_keyframe_motion_entry& e) -> std::pair<int, int> {
                                        return {e.bbox.x + e.bbox.width / 2, e.bbox.y + e.bbox.height / 2};
                                    };

                                    bool should_start = wc->keyframe_motion_buffer().last_n_match_with_displacement(
                                        n,
                                        min_disp,
                                        [](const r_keyframe_motion_entry& e) { return e.has_motion; },
                                        get_bbox_center
                                    );

                                    if(should_start)
                                    {
                                        // Find the first triggering frame
                                        size_t first_motion_idx = wc->keyframe_motion_buffer().size() - n;
                                        const auto& trigger_entry = wc->keyframe_motion_buffer().at(first_motion_idx);

                                        // Start new event
                                        wc->set_in_event(true);
                                        wc->set_event_start_ts(trigger_entry.ts);
                                        wc->set_no_motion_count(0);

                                        // Post event start with the first triggering frame
                                        _meph.post(r_vss::motion_event_start, wc->get_camera_id(), trigger_entry.ts,
                                                   trigger_entry.decoded_image, trigger_entry.width, trigger_entry.height, trigger_entry.bbox);

                                        // Post updates for subsequent frames (including current)
                                        for(size_t i = first_motion_idx + 1; i < wc->keyframe_motion_buffer().size(); ++i)
                                        {
                                            const auto& entry = wc->keyframe_motion_buffer().at(i);
                                            _meph.post(r_vss::motion_event_update, wc->get_camera_id(), entry.ts,
                                                       entry.decoded_image, entry.width, entry.height, entry.bbox);
                                        }
                                    }
                                }
                                else
                                {
                                    // Already in event
                                    if(is_significant)
                                    {
                                        // Reset no-motion counter and send update
                                        wc->set_no_motion_count(0);
                                        _meph.post(r_vss::motion_event_update, wc->get_camera_id(), work.ts,
                                                   letterbox_data, 640, 640, motion_bbox);
                                    }
                                    else
                                    {
                                        // No motion - count consecutive no-motion frames
                                        wc->set_no_motion_count(wc->get_no_motion_count() + 1);

                                        // Require 2 consecutive keyframes without motion to end event
                                        if(wc->get_no_motion_count() >= 2)
                                        {
                                            // End event
                                            wc->set_in_event(false);
                                            wc->set_no_motion_count(0);

                                            // Backfill event duration with motion=1
                                            if(wc->first_ts_valid() && wc->get_event_start_ts() > 0)
                                            {
                                                system_clock::time_point start_tp{milliseconds{wc->get_event_start_ts()}};
                                                system_clock::time_point end_tp{milliseconds{work.ts}};
                                                uint8_t motion_flag = 1;
                                                wc->ring().write_range(start_tp, end_tp, &motion_flag);
                                                wc->set_last_written_second(work.ts / 1000);
                                            }

                                            wc->set_event_start_ts(-1);
                                            _meph.post(r_vss::motion_event_end, wc->get_camera_id(), work.ts,
                                                       letterbox_data, 640, 640, motion_bbox);
                                        }
                                    }
                                }

                                // Write motion flag to storage ring (only when not in event)
                                if(!wc->get_in_event() && wc->first_ts_valid() && ((work.ts - wc->get_first_ts()) > 60000))
                                {
                                    system_clock::time_point tp{milliseconds{work.ts}};
                                    int64_t current_second = work.ts / 1000;
                                    if(current_second != wc->get_last_written_second())
                                    {
                                        uint8_t motion_flag = 0;
                                        wc->ring().write(tp, &motion_flag);
                                        wc->set_last_written_second(current_second);
                                    }
                                }

                                if(!wc->first_ts_valid())
                                    wc->set_first_ts(work.ts);
                            }

                            if(ds == r_av::R_CODEC_STATE_HAS_OUTPUT)
                                decode_again = false;
                        }
                    }
                }
            }
            catch(const std::exception& e)
            {
                R_LOG_ERROR("MOTION DECODE ERROR for camera ID: %s, work.ts = %lld, work.is_key_frame = %s: %s", work.id.c_str(), work.ts, work.is_key_frame?"true":"false", e.what());
                printf("MOTION DECODE ERROR: %s\n", e.what());
                fflush(stdout);
                _work_contexts.erase(work.id);
            }
        }
    }
}

map<string, shared_ptr<r_work_context>>::iterator r_motion_engine::_create_work_context(const r_work_item& item)
{
    auto maybe_camera = _devices.get_camera_by_id(item.id);

    if(maybe_camera.is_null())
        R_THROW(("Motion engine unable to find camera with id: %s", item.id.c_str()));

    auto camera = maybe_camera.value();

    auto wc = make_shared<r_work_context>(
        r_av::encoding_to_av_codec_id(item.video_codec_name),
        camera,
        _get_storage_path(camera.motion_detection_file_path.value(), _top_dir),
        r_pipeline::get_video_codec_extradata(item.video_codec_name, item.video_codec_parameters)
    );

    return _work_contexts.insert(make_pair(item.id, wc)).first;
}
