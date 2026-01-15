
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
    DEFAULT_BUFFER_GOPS = 4,
    // Maximum frames to queue for motion detection before dropping
    // This prevents memory exhaustion if motion processing can't keep up
    // At 30fps * 10 cameras = 300 frames/sec, 1000 frames = ~3 seconds buffer
    MOTION_ENGINE_MAX_QUEUE_SIZE = 1000
    DEFAULT_MIN_MOTION_DISPLACEMENT = 15  // pixels
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
                   double min_motion_displacement = DEFAULT_MIN_MOTION_DISPLACEMENT) :
        _motion_state(60),
        _video_decoder(codec_id),
        _camera(camera),
        _ring(path, RING_MOTION_FLAG_SIZE),
        _in_event(false),
        _first_ts(-1),
        _last_written_second(-1),
        _motion_confirm_frames(motion_confirm_frames),
        _min_motion_displacement(min_motion_displacement),
        _keyframe_motion_buffer(motion_confirm_frames)
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

    // Ring buffer accessor
    r_utils::r_ring_buffer<r_keyframe_motion_entry>& keyframe_motion_buffer() { return _keyframe_motion_buffer; }
    size_t get_motion_confirm_frames() const { return _motion_confirm_frames; }
    double get_min_motion_displacement() const { return _min_motion_displacement; }

    // No-motion counter for event end hysteresis
    size_t get_no_motion_count() const { return _no_motion_count; }
    void set_no_motion_count(size_t v) { _no_motion_count = v; }

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
    double _min_motion_displacement;

    // Ring buffer: decoded key frames with motion detection results
    r_utils::r_ring_buffer<r_keyframe_motion_entry> _keyframe_motion_buffer;

    // Counter for consecutive keyframes without motion (for event end hysteresis)
    size_t _no_motion_count {0};
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

    // Returns number of frames dropped since last call (resets counter)
    R_API size_t get_and_reset_dropped_count();

    // Returns current queue size
    R_API size_t get_queue_size() const;

private:
    void _entry_point();
    std::map<std::string, std::shared_ptr<r_work_context>>::iterator _create_work_context(const r_work_item& item);
    r_disco::r_devices& _devices;
    std::string _top_dir;
    // Bounded queue to prevent memory exhaustion if motion processing can't keep up
    r_utils::r_blocking_q<r_work_item> _work{MOTION_ENGINE_MAX_QUEUE_SIZE};
    std::map<std::string, std::shared_ptr<r_work_context>> _work_contexts;
    bool _running;
    std::thread _thread;
    r_motion_event_plugin_host& _meph;
};

}

#endif
