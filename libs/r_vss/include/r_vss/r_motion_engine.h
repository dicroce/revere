
#ifndef __r_vss_r_motion_engine_h
#define __r_vss_r_motion_engine_h

#include "r_motion/r_motion_state.h"
#include "r_utils/r_blocking_q.h"
#include "r_utils/r_macro.h"
#include "r_av/r_video_decoder.h"
#include "r_pipeline/r_gst_buffer.h"
#include "r_disco/r_devices.h"
#include "r_vss/r_motion_event_plugin_host.h"
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <functional>
#include <mutex>

namespace r_vss
{

enum
{
    RING_MOTION_EVENT_SIZE = 11
};

// Callback for motion data: (camera_id, timestamp_ms, motion_percent, avg_motion_percent, stddev_percent)
using motion_data_cb = std::function<void(const std::string&, int64_t, uint8_t, uint8_t, uint8_t)>;

struct r_work_item
{
    r_pipeline::r_gst_buffer frame;
    std::string video_codec_name;
    std::string video_codec_parameters;
    std::string id;
    int64_t ts;
    bool is_key_frame;
};

struct r_work_context
{
public:
    r_work_context(AVCodecID codec_id,
                   const r_disco::r_camera& camera,
                   const std::vector<uint8_t> ed) :
        _motion_state(60),
        _video_decoder(codec_id),
        _camera(camera),
        _in_event(false),
        _decode_all_frames(false),
        _first_ts(-1)
    {
        _video_decoder.set_extradata(ed);
    }
    ~r_work_context() noexcept
    {
    }
    r_av::r_video_decoder& decoder(){return _video_decoder;}
    r_motion::r_motion_state& motion_state(){return _motion_state;}
    bool get_in_event() const { return _in_event; }
    void set_in_event(bool v) { _in_event = v; }
    std::string get_camera_id() { return _camera.id; }
    bool get_decode_all_frames() const { return _decode_all_frames; }
    void set_decode_all_frames(bool v) { _decode_all_frames = v; }
    bool first_ts_valid() { return _first_ts != -1; }
    int64_t get_first_ts() const { return _first_ts; }
    void set_first_ts(int64_t ts) { _first_ts = ts; }
private:
    r_motion::r_motion_state _motion_state;
    r_av::r_video_decoder _video_decoder;
    r_disco::r_camera _camera;
    bool _in_event;
    bool _decode_all_frames;
    int64_t _first_ts;
};

class r_motion_engine final
{
public:
    r_motion_engine() = delete;
    R_API r_motion_engine(r_disco::r_devices& devices, const std::string& top_dir, r_motion_event_plugin_host& meph);
    r_motion_engine(const r_motion_engine&) = delete;
    r_motion_engine(r_motion_engine&&) = delete;
    R_API ~r_motion_engine() noexcept;

    r_motion_engine& operator=(const r_motion_engine&) = delete;
    r_motion_engine& operator=(r_motion_engine&&) = delete;

    R_API void start();
    R_API void stop() noexcept;

    R_API void post_frame(r_pipeline::r_gst_buffer buffer, int64_t ts, const std::string& video_codec_name, const std::string& video_codec_parameters, const std::string& id, bool is_key_frame);

    R_API void remove_work_context(const std::string& camera_id);

    // Register callback for motion data - called when motion is detected
    R_API void set_motion_data_cb(const std::string& camera_id, motion_data_cb cb);
    R_API void clear_motion_data_cb(const std::string& camera_id);

private:
    void _entry_point();
    std::map<std::string, std::shared_ptr<r_work_context>>::iterator _create_work_context(const r_work_item& item);
    r_disco::r_devices& _devices;
    std::string _top_dir;
    r_utils::r_blocking_q<r_work_item> _work;
    std::map<std::string, std::shared_ptr<r_work_context>> _work_contexts;
    std::map<std::string, motion_data_cb> _motion_data_cbs;
    std::mutex _motion_data_cbs_mutex;
    bool _running;
    std::thread _thread;
    r_motion_event_plugin_host& _meph;
};

}

#endif
