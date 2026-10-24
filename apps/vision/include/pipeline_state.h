
#ifndef __vision_pipeline_state_h
#define __vision_pipeline_state_h

#include "r_pipeline/r_gst_source.h"
#include "r_utils/r_work_q.h"
#include "r_utils/r_nullable.h"
#include "r_av/r_video_decoder.h"
#include "r_av/r_audio_decoder.h"
#include <SDL.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdint>
#include "stream_info.h"
#include "sample.h"
#include "pipeline_host.h"
#include "configure_state.h"

namespace vision
{

class pipeline_state final
{
public:
    pipeline_state(const stream_info& si, pipeline_host* ph, uint16_t w, uint16_t h, configure_state& cfg_state);
    ~pipeline_state();

    void resize(uint16_t w, uint16_t h);

    void play_live();
    void play();
    // Play the recorded range [start, end). Also used by the gap-skipping
    // auto-advance in pipeline_host, which picks its own range from /contents.
    void play(const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end);
    void stop();
    bool running() const {return _source.running();}

    // True when the last play was a recorded range (play()), false for live.
    inline bool is_playback() const {return _playback;}
    inline std::chrono::system_clock::time_point play_start() const {return _play_start;}
    inline std::chrono::system_clock::time_point play_end() const {return _play_end;}
    inline std::chrono::system_clock::duration range_width() const {return _range_end - _range_start;}

    // Bumped by every play()/play_live()/stop(), so a caller that released the
    // lock can tell whether the pipe it inspected is still the one it sees.
    inline uint32_t play_generation() const {return _play_generation;}

    inline bool received_frame_since_play() const {return _frames_since_play.load() > 0;}

    // Time since the last video frame arrived from the source, or since play()
    // if none has yet. Used to detect the end of a playback range (no EOS is
    // surfaced by r_gst_source, so the end shows up as the stream going quiet).
    inline std::chrono::milliseconds since_last_video_frame() const
    {
        auto now = std::chrono::steady_clock::now();
        if(_frames_since_play.load() == 0)
            return std::chrono::duration_cast<std::chrono::milliseconds>(now - _last_play_time);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return std::chrono::milliseconds(now_ms - _last_v_arrival_ms.load());
    }

    // Gap-skip bookkeeping (pipeline_host, under its lock): consecutive
    // auto-advances that made no progress, and where the last one started.
    inline int auto_advance_streak() const {return _auto_advance_streak;}
    inline void set_auto_advance_streak(int n) {_auto_advance_streak = n;}
    inline std::chrono::system_clock::time_point last_auto_advance_pos() const {return _last_auto_advance_pos;}
    inline void set_last_auto_advance_pos(const std::chrono::system_clock::time_point& p) {_last_auto_advance_pos = p;}

    void control_bar(const std::chrono::system_clock::time_point& pos);

    inline std::string name() const {return _si.name;}
    inline uint16_t width() const {return _w;}
    inline uint16_t height() const {return _h;}

    inline bool has_audio() const {return _has_audio;}
    inline int64_t last_v_pts() const {return _last_v_pts;}
    inline int64_t last_a_pts() const {return _last_a_pts;}

    inline int64_t v_pts_at_check() const {return _v_pts_at_check;}
    inline void set_v_pts_at_check(int64_t v_pts) {_v_pts_at_check = v_pts;}
    inline int64_t a_pts_at_check() const {return _a_pts_at_check;}
    inline void set_a_pts_at_check(int64_t a_pts) {_a_pts_at_check = a_pts;}

    // Before the first frame arrives use a short fuse — a stream that hasn't
    // produced anything in 3 s is almost certainly stuck and should be retried
    // immediately. Once frames are flowing use the original 15 s warmup so a
    // momentary stall doesn't trigger a spurious restart.
    inline bool ready_for_dead_check() const
    {
        auto elapsed = std::chrono::steady_clock::now() - _last_play_time;
        auto threshold = _received_first_frame.load()
            ? std::chrono::seconds(15)
            : std::chrono::seconds(8);
        return elapsed > threshold;
    }

    inline void update_range(std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end)
    {
        _range_start = start;
        _range_end = end;
    }

    bool playing() const
    {
        return _source.running();
    }

    void set_audio_active(bool active)
    {
        _audio_active.store(active);
        if(_sdl_audio_device != 0)
        {
            if(!active)
                SDL_ClearQueuedAudio(_sdl_audio_device);
            SDL_PauseAudioDevice(_sdl_audio_device, active ? 0 : 1);
        }
    }

    void set_volume_gain(float gain)
    {
        float prev = _volume_gain.exchange(gain);
        if(_sdl_audio_device != 0 && prev != gain)
            SDL_ClearQueuedAudio(_sdl_audio_device);
    }
    
    inline std::chrono::system_clock::time_point get_last_control_bar_pos() const 
    {
        return _last_control_bar_pos;
    }

    // Where the next play() starts from. Pause sets this to the time of the
    // frame left on screen so Resume (Play) continues from exactly there.
    inline void set_last_control_bar_pos(const std::chrono::system_clock::time_point& pos)
    {
        _last_control_bar_pos = pos;
    }

private:
    void _entry_point();

    stream_info _si;
    pipeline_host* _ph;
    uint16_t _w;
    uint16_t _h;
    r_pipeline::r_gst_source _source;
    bool _running;
    std::thread _process_th;
    r_utils::r_work_q<sample, bool> _process_q;
    r_utils::r_nullable<sample> _last_video_sample;
    r_utils::r_nullable<r_av::r_video_decoder> _video_decoder;
    r_utils::r_nullable<r_av::r_audio_decoder> _audio_decoder;
    SDL_AudioDeviceID _sdl_audio_device;
    std::atomic<float> _volume_gain;
    std::atomic<bool> _audio_active;
    bool _has_audio;
    std::atomic<bool> _received_first_frame;
    int64_t _last_v_pts;
    int64_t _last_a_pts;

    int64_t _v_pts_at_check;
    int64_t _a_pts_at_check;

    configure_state& _cfg_state;

    std::chrono::system_clock::time_point _last_control_bar_pos;
    std::chrono::system_clock::time_point _range_start;
    std::chrono::system_clock::time_point _range_end;
    std::chrono::steady_clock::time_point _last_play_time;

    // Playback-range / gap-skip state (see is_playback() and friends).
    bool _playback {false};
    std::chrono::system_clock::time_point _play_start;
    std::chrono::system_clock::time_point _play_end;
    uint32_t _play_generation {0};
    std::atomic<uint32_t> _frames_since_play {0};
    std::atomic<int64_t> _last_v_arrival_ms {0}; // steady_clock ms, video callback thread
    int _auto_advance_streak {0};
    std::chrono::system_clock::time_point _last_auto_advance_pos;

    // Pre-decode arrival instrumentation (vision_net log line): measured in the
    // GStreamer video sample callback, before decode and before any vision
    // queueing, so upstream (camera/server/network) delivery jitter can be told
    // apart from jitter added inside vision. Compare against vision_stats
    // arr_jit, which is measured post-decode. Touched only on the callback thread.
    std::chrono::steady_clock::time_point _net_last_arrival;
    int64_t _net_last_pts {0};
    bool _net_have_last {false};
    uint32_t _net_n {0};
    uint32_t _net_nonmono {0};
    double _net_pts_d_sum {0.0};
    int64_t _net_pts_d_min {0};
    int64_t _net_pts_d_max {0};
    double _net_jit_sum {0.0};
    int64_t _net_jit_max {0};
    std::chrono::steady_clock::time_point _net_window_start;
    bool _net_window_started {false};

    // TEMP DIAGNOSTIC (vision_pair log line): identify what the buffers in
    // <=2ms pts pairs actually are (SEI-only AU? duplicate picture? slice?).
    // Remove once the upstream timestamp pathology is fixed.
    uint32_t _diag_pair_logs {0};
    size_t _last_buf_size {0};
    std::string _last_nal_types;
    bool _last_buf_key {false};
};

}

#endif
