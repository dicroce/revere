
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
    void stop();
    bool running() const {return _source.running();}

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
};

}

#endif
