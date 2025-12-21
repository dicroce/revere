
#include "r_vss/r_motion_engine.h"
#include "r_vss/r_motion_event.h"
#include "r_vss/r_vss_utils.h"
#include "r_motion/utils.h"
#include "r_av/r_muxer.h"
#include "r_av/r_codec_state.h"
#include "r_pipeline/r_stream_info.h"
#include "r_pipeline/r_gst_buffer.h"
#include "r_utils/r_file.h"
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

                // Smart decode logic: decode key frames always, P frames only during motion
                bool should_decode = work.is_key_frame || found_wc->second->get_decode_all_frames();

                // Skip p-frames unless we've found motion
                if(!should_decode)
                    continue;

                auto mi = work.frame.map(r_pipeline::r_gst_buffer::MT_READ);

                int max_decode_attempts = 10;

                found_wc->second->decoder().attach_buffer(mi.data(), mi.size());

                bool decode_again = true;
                while(decode_again)
                {
                    if(max_decode_attempts <= 0)
                        R_THROW(("Unable to decode!"));
                    --max_decode_attempts;

                    auto ds = found_wc->second->decoder().decode();
                    if(ds == r_av::R_CODEC_STATE_HAS_OUTPUT || ds == r_av::R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                    {
                        // Calculate aspect-correct height for 640 width (works for both mobilenet and yolov8)
                        uint16_t input_w = found_wc->second->decoder().input_width();
                        uint16_t input_h = found_wc->second->decoder().input_height();
                        uint16_t output_w = 640;
                        uint16_t output_h = (uint16_t)((640.0 * input_h) / input_w);
                        
                        auto decoded = found_wc->second->decoder().get(AV_PIX_FMT_RGB24, output_w, output_h, 1);


                        r_motion::r_image img;
                        img.type = r_motion::R_MOTION_IMAGE_TYPE_RGB;
                        img.width = output_w;
                        img.height = output_h;
                        img.data = *decoded;

                        auto maybe_motion_info = found_wc->second->motion_state().process(img);

                        if(!maybe_motion_info.is_null())
                        {
                            auto motion_info = maybe_motion_info.value();
                            auto ts = work.ts;

                            bool is_significant = is_motion_significant(motion_info.motion, motion_info.avg_motion, motion_info.stddev);

                            system_clock::time_point tp{milliseconds{ts}};

                            if(found_wc->second->first_ts_valid() && ((ts - found_wc->second->get_first_ts()) > 60000))
                            {
                                // Write to ring buffer only on second rollover
                                int64_t current_second = ts / 1000;
                                if(current_second != found_wc->second->get_last_written_second())
                                {
                                    uint8_t motion_flag = is_significant ? 1 : 0;
                                    found_wc->second->ring().write(tp, &motion_flag);
                                    found_wc->second->set_last_written_second(current_second);
                                }

                                // Convert motion region from r_motion to r_vss format
                                r_vss::motion_region motion_bbox;
                                motion_bbox.x = motion_info.motion_bbox.x;
                                motion_bbox.y = motion_info.motion_bbox.y;
                                motion_bbox.width = motion_info.motion_bbox.width;
                                motion_bbox.height = motion_info.motion_bbox.height;
                                motion_bbox.has_motion = motion_info.motion_bbox.has_motion;

                                if(is_significant)
                                {
                                    if(!found_wc->second->get_in_event())
                                    {
                                        found_wc->second->set_in_event(true);
                                        found_wc->second->set_decode_all_frames(true); // Start decoding all frames
                                        _meph.post(r_vss::motion_event_start, found_wc->second->get_camera_id(), work.ts, *decoded, output_w, output_h, motion_bbox);
                                    }
                                    else _meph.post(r_vss::motion_event_update, found_wc->second->get_camera_id(), work.ts, *decoded, output_w, output_h, motion_bbox);
                                }
                                else
                                {
                                    if(found_wc->second->get_in_event())
                                    {
                                        found_wc->second->set_in_event(false);
                                        found_wc->second->set_decode_all_frames(false); // Stop decoding all frames
                                        _meph.post(r_vss::motion_event_end, found_wc->second->get_camera_id(), work.ts, *decoded, output_w, output_h, motion_bbox);
                                    }
                                }

                                // Send frame to plugins if we're in a motion event (regardless of key frame status)
                                if(found_wc->second->get_in_event())
                                {
                                    // For key frames: we already posted above
                                    // For P frames during motion: send update event
                                    if(!work.is_key_frame)
                                    {
                                        _meph.post(r_vss::motion_event_update, found_wc->second->get_camera_id(), work.ts, *decoded, output_w, output_h, motion_bbox);
                                    }
                                }
                            }

                            if(!found_wc->second->first_ts_valid())
                                found_wc->second->set_first_ts(ts);
                        }

                        if(ds == r_av::R_CODEC_STATE_HAS_OUTPUT)
                            decode_again = false;
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
