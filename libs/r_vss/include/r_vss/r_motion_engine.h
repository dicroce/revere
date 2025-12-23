
#ifndef __r_vss_r_motion_engine_h
#define __r_vss_r_motion_engine_h

#include "r_motion/r_motion_state.h"
#include "r_utils/r_blocking_q.h"
#include "r_utils/r_macro.h"
#include "r_utils/r_ring_buffer.h"
#include "r_av/r_video_decoder.h"
#include "r_pipeline/r_gst_buffer.h"
#include "r_disco/r_devices.h"
#include "r_storage/r_ring.h"
#include "r_vss/r_motion_event_plugin_host.h"
#include <vector>
#include <map>
#include <memory>
#include <thread>

namespace r_vss
{

enum
{
    RING_MOTION_FLAG_SIZE = 1,
    DEFAULT_MOTION_CONFIRM_FRAMES = 3,
    DEFAULT_MOTION_END_FRAMES = 3,      // Require N frames to END event
    DEFAULT_GOP_SIZE = 30,
    DEFAULT_BUFFER_GOPS = 4
};

// Entry for ring buffer 1: key frame motion detection results
struct r_keyframe_motion_entry
{
    int64_t ts;
    bool has_motion;
    std::vector<uint8_t> decoded_image;
    uint16_t width;
    uint16_t height;
    motion_region bbox;
};

// Entry for ring buffer 2: encoded frames (I and P)
struct r_encoded_frame_entry
{
    r_pipeline::r_gst_buffer frame;
    int64_t ts;
    bool is_key_frame;
};

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
                   const std::string& path,
                   const std::vector<uint8_t> ed,
                   size_t motion_confirm_frames = DEFAULT_MOTION_CONFIRM_FRAMES,
                   size_t motion_end_frames = DEFAULT_MOTION_END_FRAMES,
                   size_t gop_size = DEFAULT_GOP_SIZE,
                   size_t buffer_gops = DEFAULT_BUFFER_GOPS) :
        _motion_state(60),
        _video_decoder(codec_id),
        _camera(camera),
        _ring(path, RING_MOTION_FLAG_SIZE),
        _in_event(false),
        _first_ts(-1),
        _last_written_second(-1),
        _motion_confirm_frames(motion_confirm_frames),
        _motion_end_frames(motion_end_frames),
        _keyframe_motion_buffer(std::max(motion_confirm_frames, motion_end_frames)),
        _encoded_frame_buffer(gop_size * buffer_gops),
        _processing_catchup(false)
    {
        _video_decoder.set_extradata(ed);
    }
    ~r_work_context() noexcept
    {
    }
    r_av::r_video_decoder& decoder(){return _video_decoder;}
    r_motion::r_motion_state& motion_state(){return _motion_state;}
    r_storage::r_ring& ring(){return _ring;}
    bool get_in_event() const { return _in_event; }
    void set_in_event(bool v) { _in_event = v; }
    int64_t get_event_start_ts() const { return _event_start_ts; }
    void set_event_start_ts(int64_t ts) { _event_start_ts = ts; }
    std::string get_camera_id() { return _camera.id; }
    bool first_ts_valid() { return _first_ts != -1; }
    int64_t get_first_ts() const { return _first_ts; }
    void set_first_ts(int64_t ts) { _first_ts = ts; }
    int64_t get_last_written_second() const { return _last_written_second; }
    void set_last_written_second(int64_t s) { _last_written_second = s; }

    // Ring buffer accessors
    r_utils::r_ring_buffer<r_keyframe_motion_entry>& keyframe_motion_buffer() { return _keyframe_motion_buffer; }
    r_utils::r_ring_buffer<r_encoded_frame_entry>& encoded_frame_buffer() { return _encoded_frame_buffer; }
    size_t get_motion_confirm_frames() const { return _motion_confirm_frames; }
    size_t get_motion_end_frames() const { return _motion_end_frames; }

    // Catchup processing state
    bool is_processing_catchup() const { return _processing_catchup; }
    void set_processing_catchup(bool v) { _processing_catchup = v; }

private:
    r_motion::r_motion_state _motion_state;
    r_av::r_video_decoder _video_decoder;
    r_disco::r_camera _camera;
    r_storage::r_ring _ring;
    bool _in_event;
    int64_t _event_start_ts {-1};  // Timestamp when current event started
    int64_t _first_ts;
    int64_t _last_written_second;

    // Motion confirmation settings
    size_t _motion_confirm_frames;
    size_t _motion_end_frames;

    // Ring buffer 1: decoded key frames with motion detection results
    r_utils::r_ring_buffer<r_keyframe_motion_entry> _keyframe_motion_buffer;

    // Ring buffer 2: all encoded frames (I and P) for catchup processing
    r_utils::r_ring_buffer<r_encoded_frame_entry> _encoded_frame_buffer;

    // True while processing buffered frames after motion detection
    bool _processing_catchup;
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

private:
    void _entry_point();
    std::map<std::string, std::shared_ptr<r_work_context>>::iterator _create_work_context(const r_work_item& item);
    void _process_catchup_frames(std::shared_ptr<r_work_context>& wc, int64_t start_ts);
    void _decode_and_process_frame(std::shared_ptr<r_work_context>& wc, const r_encoded_frame_entry& entry, bool is_catchup);
    r_disco::r_devices& _devices;
    std::string _top_dir;
    r_utils::r_blocking_q<r_work_item> _work;
    std::map<std::string, std::shared_ptr<r_work_context>> _work_contexts;
    bool _running;
    std::thread _thread;
    r_motion_event_plugin_host& _meph;
};

}

#endif
