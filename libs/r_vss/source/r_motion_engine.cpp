
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
#include <chrono>

using namespace r_vss;
using namespace r_utils;
using namespace r_storage;
using namespace r_motion;
using namespace std;
using namespace std::chrono;

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

                // Always push encoded frames to ring buffer 2 (all frames buffer)
                r_encoded_frame_entry encoded_entry;
                encoded_entry.frame = work.frame;
                encoded_entry.ts = work.ts;
                encoded_entry.is_key_frame = work.is_key_frame;
                wc->encoded_frame_buffer().push(encoded_entry);

                // If we're in a motion event, decode and process every frame
                if(wc->get_in_event())
                {
                    _decode_and_process_frame(wc, encoded_entry, false);
                }
                // If not in motion event, only decode key frames for motion detection
                else if(work.is_key_frame)
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
                            uint16_t output_w = 640;
                            uint16_t output_h = (uint16_t)((640.0 * input_h) / input_w);

                            auto decoded = wc->decoder().get(AV_PIX_FMT_RGB24, output_w, output_h, 1);

                            r_motion::r_image img;
                            img.type = r_motion::R_MOTION_IMAGE_TYPE_RGB;
                            img.width = output_w;
                            img.height = output_h;
                            img.data = *decoded;

                            auto maybe_motion_info = wc->motion_state().process(img);

                            if(!maybe_motion_info.is_null())
                            {
                                auto motion_info = maybe_motion_info.value();
                                bool is_significant = is_motion_significant(motion_info.motion, motion_info.avg_motion, motion_info.stddev);

                                // Convert motion region from r_motion to r_vss format
                                r_vss::motion_region motion_bbox;
                                motion_bbox.x = motion_info.motion_bbox.x;
                                motion_bbox.y = motion_info.motion_bbox.y;
                                motion_bbox.width = motion_info.motion_bbox.width;
                                motion_bbox.height = motion_info.motion_bbox.height;
                                motion_bbox.has_motion = motion_info.motion_bbox.has_motion;

                                // Push to keyframe motion buffer (ring buffer 1)
                                r_keyframe_motion_entry kf_entry;
                                kf_entry.ts = work.ts;
                                kf_entry.has_motion = is_significant;
                                kf_entry.decoded_image = *decoded;
                                kf_entry.width = output_w;
                                kf_entry.height = output_h;
                                kf_entry.bbox = motion_bbox;
                                wc->keyframe_motion_buffer().push(kf_entry);

                                // Check if we should transition to IN_MOTION state
                                // Require N consecutive key frames with motion
                                size_t n = wc->get_motion_confirm_frames();
                                bool should_start_motion = wc->keyframe_motion_buffer().last_n_match(
                                    n, [](const r_keyframe_motion_entry& e) { return e.has_motion; }
                                );

                                if(should_start_motion && !wc->get_in_event())
                                {
                                    // Find the timestamp of the first frame that triggered motion
                                    // This is the (n-1)th from newest in the buffer
                                    size_t first_motion_idx = wc->keyframe_motion_buffer().size() - n;
                                    int64_t trigger_ts = wc->keyframe_motion_buffer().at(first_motion_idx).ts;
                                    const auto& trigger_entry = wc->keyframe_motion_buffer().at(first_motion_idx);

                                    R_LOG_INFO("MOTION: STARTING EVENT - %zu consecutive frames with motion, trigger_ts=%lld",
                                               n, (long long)trigger_ts);

                                    // Transition to IN_MOTION and record event start time
                                    wc->set_in_event(true);
                                    wc->set_event_start_ts(trigger_ts);

                                    // Note: We don't write to ring buffer here - we'll use write_range
                                    // when the event ends to fill the entire event duration with 1s

                                    // Post motion_event_start with the triggering frame
                                    _meph.post(r_vss::motion_event_start, wc->get_camera_id(), trigger_ts,
                                               trigger_entry.decoded_image, trigger_entry.width, trigger_entry.height, trigger_entry.bbox);

                                    // Process catchup frames from the encoded buffer
                                    _process_catchup_frames(wc, trigger_ts);
                                }
                                // Only write to storage ring if NOT in event (during event, it's handled in _decode_and_process_frame)
                                else if(!wc->get_in_event())
                                {
                                    // Don't write unconfirmed motion to storage ring
                                    // Only write confirmed no-motion (when not in event and no pending motion)
                                    if(!is_significant && wc->first_ts_valid() && ((work.ts - wc->get_first_ts()) > 60000))
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
                printf("MOTION DECODE ERROR: %s\n", e.what());
                fflush(stdout);
                _work_contexts.erase(work.id);
            }
        }
    }
}

void r_motion_engine::_process_catchup_frames(shared_ptr<r_work_context>& wc, int64_t start_ts)
{
    wc->set_processing_catchup(true);

    auto& buffer = wc->encoded_frame_buffer();

    // Find the starting index in the encoded buffer
    // We need to find the key frame with this timestamp
    size_t start_idx = 0;
    bool found_start = false;

    for(size_t i = 0; i < buffer.size(); ++i)
    {
        const auto& entry = buffer.at(i);
        if(entry.ts == start_ts && entry.is_key_frame)
        {
            start_idx = i;
            found_start = true;
            break;
        }
    }

    if(!found_start)
    {
        // If we can't find the exact frame, start from the first key frame after start_ts
        for(size_t i = 0; i < buffer.size(); ++i)
        {
            const auto& entry = buffer.at(i);
            if(entry.is_key_frame && entry.ts >= start_ts)
            {
                start_idx = i;
                found_start = true;
                break;
            }
        }
    }

    if(!found_start)
    {
        wc->set_processing_catchup(false);
        return;
    }

    // Skip the first frame (already sent as motion_event_start)
    // Process remaining frames from start_idx+1 to end
    size_t catchup_count = buffer.size() - start_idx - 1;
    R_LOG_INFO("MOTION: Processing %zu catchup frames from idx %zu to %zu", catchup_count, start_idx + 1, buffer.size() - 1);

    for(size_t i = start_idx + 1; i < buffer.size(); ++i)
    {
        _decode_and_process_frame(wc, buffer.at(i), true);
    }

    R_LOG_INFO("MOTION: Catchup complete");
    wc->set_processing_catchup(false);
}

void r_motion_engine::_decode_and_process_frame(shared_ptr<r_work_context>& wc, const r_encoded_frame_entry& entry, bool is_catchup)
{
    auto mi = entry.frame.map(r_pipeline::r_gst_buffer::MT_READ);
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
            uint16_t output_w = 640;
            uint16_t output_h = (uint16_t)((640.0 * input_h) / input_w);

            auto decoded = wc->decoder().get(AV_PIX_FMT_RGB24, output_w, output_h, 1);

            r_motion::r_image img;
            img.type = r_motion::R_MOTION_IMAGE_TYPE_RGB;
            img.width = output_w;
            img.height = output_h;
            img.data = *decoded;

            // During catchup processing, pass skip_update=true to prevent the motion detector
            // from adapting its baseline to the motion being detected. This keeps the baseline
            // stable so that subsequent live frames are evaluated correctly.
            auto maybe_motion_info = wc->motion_state().process(img, is_catchup);

            r_vss::motion_region motion_bbox = {0, 0, 0, 0, false};
            bool is_significant = false;

            if(!maybe_motion_info.is_null())
            {
                auto motion_info = maybe_motion_info.value();
                is_significant = is_motion_significant(motion_info.motion, motion_info.avg_motion, motion_info.stddev);

                motion_bbox.x = motion_info.motion_bbox.x;
                motion_bbox.y = motion_info.motion_bbox.y;
                motion_bbox.width = motion_info.motion_bbox.width;
                motion_bbox.height = motion_info.motion_bbox.height;
                motion_bbox.has_motion = motion_info.motion_bbox.has_motion;

                // For key frames during live processing (not catchup), update the keyframe motion buffer
                if(entry.is_key_frame && !is_catchup)
                {
                    r_keyframe_motion_entry kf_entry;
                    kf_entry.ts = entry.ts;
                    kf_entry.has_motion = is_significant;
                    kf_entry.decoded_image = *decoded;
                    kf_entry.width = output_w;
                    kf_entry.height = output_h;
                    kf_entry.bbox = motion_bbox;
                    wc->keyframe_motion_buffer().push(kf_entry);

                    // Write motion flag to storage ring buffer
                    // Only write when NOT in an event - events are backfilled with write_range on end
                    if(!wc->get_in_event() && wc->first_ts_valid() && ((entry.ts - wc->get_first_ts()) > 60000))
                    {
                        system_clock::time_point tp{milliseconds{entry.ts}};
                        int64_t current_second = entry.ts / 1000;
                        if(current_second != wc->get_last_written_second())
                        {
                            uint8_t motion_flag = is_significant ? 1 : 0;
                            wc->ring().write(tp, &motion_flag);
                            wc->set_last_written_second(current_second);
                        }
                    }

                    // Check if we should transition to NO_MOTION state
                    // Require N consecutive key frames without motion (more than start threshold)
                    size_t end_n = wc->get_motion_end_frames();
                    bool should_stop_motion = wc->keyframe_motion_buffer().last_n_match(
                        end_n, [](const r_keyframe_motion_entry& e) { return !e.has_motion; }
                    );

                    if(should_stop_motion && wc->get_in_event())
                    {
                        R_LOG_INFO("MOTION: ENDING EVENT - %zu consecutive frames without motion", end_n);

                        // Backfill the entire event duration with motion=1 using write_range
                        if(wc->first_ts_valid() && wc->get_event_start_ts() > 0)
                        {
                            system_clock::time_point start_tp{milliseconds{wc->get_event_start_ts()}};
                            system_clock::time_point end_tp{milliseconds{entry.ts}};
                            uint8_t motion_flag = 1;
                            wc->ring().write_range(start_tp, end_tp, &motion_flag);
                            wc->set_last_written_second(entry.ts / 1000);
                        }

                        wc->set_in_event(false);
                        wc->set_event_start_ts(-1);
                        _meph.post(r_vss::motion_event_end, wc->get_camera_id(), entry.ts, *decoded, output_w, output_h, motion_bbox);

                        if(ds == r_av::R_CODEC_STATE_HAS_OUTPUT)
                            decode_again = false;
                        return;
                    }
                }
            }

            // Send update event for frames during motion event
            if(wc->get_in_event())
            {
                _meph.post(r_vss::motion_event_update, wc->get_camera_id(), entry.ts, *decoded, output_w, output_h, motion_bbox);
            }

            if(ds == r_av::R_CODEC_STATE_HAS_OUTPUT)
                decode_again = false;
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
        _top_dir + PATH_SLASH + "video" + PATH_SLASH + camera.motion_detection_file_path.value(),
        r_pipeline::get_video_codec_extradata(item.video_codec_name, item.video_codec_parameters)
    );

    return _work_contexts.insert(make_pair(item.id, wc)).first;
}
