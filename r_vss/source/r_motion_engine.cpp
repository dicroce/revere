
#include "r_vss/r_motion_engine.h"
#include "r_motion/utils.h"
#include "r_mux/r_muxer.h"
#include "r_codec/r_codec_state.h"
#include "r_pipeline/r_stream_info.h"
#include "r_pipeline/r_gst_buffer.h"
#include "r_utils/r_file.h"
#include <chrono>

using namespace r_vss;
using namespace r_utils;
using namespace r_storage;
using namespace std;
using namespace std::chrono;

r_motion_engine::r_motion_engine(r_disco::r_devices& devices, const string& top_dir) :
    _devices(devices),
    _top_dir(top_dir)
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

void r_motion_engine::post_frame(r_pipeline::r_gst_buffer buffer, int64_t ts, const string& video_codec_name, const string& video_codec_parameters, const string& id)
{
    r_work_item item;
    item.frame = buffer;
    item.video_codec_name = video_codec_name;
    item.video_codec_parameters = video_codec_parameters;
    item.id = id;
    item.ts = ts;

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

            try
            {
                auto found_wc = _work_contexts.find(work.id);
                if(found_wc == _work_contexts.end())
                    found_wc = _create_work_context(work);

                auto mi = work.frame.map(r_pipeline::r_gst_buffer::MT_READ);

                bool decode_again = true;
                while(decode_again)
                {
                    found_wc->second->decoder().attach_buffer(mi.data(), mi.size());
                    auto ds = found_wc->second->decoder().decode();
                    if(ds == r_codec::R_CODEC_STATE_HAS_OUTPUT || ds == r_codec::R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                    {
                        auto decoded = found_wc->second->decoder().get(AV_PIX_FMT_ARGB, 320, 240);

                        r_motion::r_image img;
                        img.type = r_motion::R_MOTION_IMAGE_TYPE_ARGB;
                        img.width = 320;
                        img.height = 240;
                        img.data = decoded;

                        auto maybe_motion_info = found_wc->second->motion_state().process(img);

                        if(!maybe_motion_info.is_null())
                        {
                            auto motion_info = maybe_motion_info.value();

                            // a motion event consists of 3 values:
                            //     motion
                            //     average motion at that moment
                            //     running stddev
                            //
                            // motion is expressed as a uint8_t scalar value between 0 and 100 and it represents the amount of motion pixels on the screen.
                            // average motion is a uint8_t scalar that is the current average of motion values.
                            // running stddev is

                            uint64_t max_motion = 2000000;

                            uint8_t current_motion = (uint8_t)(((double)motion_info.motion / max_motion)*100.0);
                            uint8_t avg_motion = (uint8_t)(((double)motion_info.avg_motion / max_motion)*100.0);
                            uint8_t stddev = (uint8_t)(((double)motion_info.stddev / max_motion)*100.0);

                            // if the current_motion is greater than the average motion and that difference is greater
                            // than 50% of the stanard deviation, then we have motion.

                            if(current_motion > avg_motion &&
                               current_motion - avg_motion > ((double)stddev * 0.5))
                            {
                                printf("MOTION! %u\n", current_motion);
                            }

                            uint8_t md[RING_MOTION_EVENT_SIZE];
                            memcpy(md, (uint8_t*)&work.ts, 8);

                            md[8] = current_motion;
                            md[9] = avg_motion;
                            md[10] = stddev;

                            time_point<system_clock, milliseconds> tp{milliseconds{work.ts}};

                            found_wc->second->ring().write(tp, &md[0]);
                        }

                        if(ds == r_codec::R_CODEC_STATE_HAS_OUTPUT)
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
        r_mux::encoding_to_av_codec_id(item.video_codec_name),
        camera,
        _top_dir + PATH_SLASH + "video" + PATH_SLASH + camera.motion_detection_file_path.value(),
        r_pipeline::get_video_codec_extradata(item.video_codec_name, item.video_codec_parameters)
    );

    return _work_contexts.insert(make_pair(item.id, wc)).first;
}
