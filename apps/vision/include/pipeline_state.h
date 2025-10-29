
#ifndef __vision_pipeline_state_h
#define __vision_pipeline_state_h

#include "r_pipeline/r_gst_source.h"
#include "r_utils/r_work_q.h"
#include "r_utils/r_nullable.h"
#include "r_av/r_video_decoder.h"
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

    inline uint16_t width() const {return _w;}
    inline uint16_t height() const {return _h;}

    inline bool has_audio() const {return _has_audio;}
    inline int64_t last_v_pts() const {return _last_v_pts;}
    inline int64_t last_a_pts() const {return _last_a_pts;}

    inline int64_t v_pts_at_check() const {return _v_pts_at_check;}
    inline void set_v_pts_at_check(int64_t v_pts) {_v_pts_at_check = v_pts;}
    inline int64_t a_pts_at_check() const {return _a_pts_at_check;}
    inline void set_a_pts_at_check(int64_t a_pts) {_a_pts_at_check = a_pts;}

    inline void update_range(std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end)
    {
        _range_start = start;
        _range_end = end;
    }

    bool playing() const
    {
        return _source.running();
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
    bool _has_audio;
    int64_t _last_v_pts;
    int64_t _last_a_pts;

    int64_t _v_pts_at_check;
    int64_t _a_pts_at_check;

    configure_state& _cfg_state;

    std::chrono::system_clock::time_point _last_control_bar_pos;
    std::chrono::system_clock::time_point _range_start;
    std::chrono::system_clock::time_point _range_end;
};

}

#endif
