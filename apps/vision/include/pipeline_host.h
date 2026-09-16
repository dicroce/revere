
#ifndef __vision_pipeline_host_h
#define __vision_pipeline_host_h

#include "r_utils/r_nullable.h"
#include <string>
#include <map>
#include <deque>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <SDL.h>
#include "layouts.h"
#include "configure_state.h"
#include "stream_info.h"
#include "sample.h"
#include "timerange.h"
#include "render_context.h"
#include "frame.h"
#include "control_bar.h"

namespace vision
{

// Presentation pacing (see pipeline_host::load_video_textures)
constexpr size_t PRESENTATION_MAX_QUEUE = 12;           // decoded frames buffered per stream; must
                                                        // hold PRESENTATION_MAX_DESIRED_BUFFER_MS of
                                                        // content at typical frame rates (12 ≈ 480ms
                                                        // at 25fps)
constexpr int64_t PRESENTATION_CUSHION_MS = 50;         // initial buffer target for a new stream
constexpr int64_t PRESENTATION_DISCONTINUITY_MS = 1000; // pts jump that forces a re-anchor
constexpr int64_t PRESENTATION_LATE_MS = 20;            // presented this far past due counts as late
constexpr int64_t PRESENTATION_RETARD_STEP_MS = 15;     // max latency added per late present
constexpr double PRESENTATION_MAX_DESIRED_BUFFER_MS = 400.0; // ceiling for the learned buffer target
constexpr double PRESENTATION_DESIRED_DECAY_MS = 0.02;  // buffer-target decay per on-time present
                                                        // (~0.5 ms/s at 25fps — slowly reclaims
                                                        // latency when delivery behaves)
constexpr double PRESENTATION_DRAIN_GAIN = 0.05;        // proportional drain of buffer above target
constexpr double PRESENTATION_DRAIN_MAX_MS = 2.0;       // max drain per present
constexpr int64_t STATS_WINDOW_SECONDS = 10;            // smoothness stats logging interval

// Log-only smoothness instrumentation, accumulated per stream and emitted as
// one log line per stats window. Interpretation:
//   pts_d   — delta between consecutive frame timestamps. Uneven values mean
//             the camera itself stamps jittery timestamps (e.g. at send time
//             rather than capture time).
//   arr_jit — |arrival interval − pts interval|: network + encoder delivery
//             jitter. Sustained spikes larger than PRESENTATION_CUSHION_MS
//             mean the cushion is too small to absorb them.
//   late    — frames presented more than PRESENTATION_LATE_MS past their due
//             time (they arrived too late to pace; jitter passed through).
struct stream_stats final
{
    // arrival side (post_video_frame)
    std::chrono::steady_clock::time_point last_arrival_wall;
    int64_t last_arrival_pts {0};
    bool have_last_arrival {false};
    uint32_t arrivals {0};
    double pts_delta_sum {0.0};
    int64_t pts_delta_min {0};
    int64_t pts_delta_max {0};
    double jitter_sum {0.0};
    int64_t jitter_max {0};
    uint32_t nonmono {0}; // frames whose pts delta was <= 0 (duplicates/backwards)
    uint32_t overflow_drops {0};

    // presentation side (load_video_textures)
    uint32_t presents {0};
    uint32_t late_presents {0};
    double lateness_sum {0.0};
    int64_t lateness_max {0};
    uint32_t skips {0};
    uint32_t reanchors {0};
    uint32_t drain_slews {0};
    uint32_t retard_slews {0};
    size_t q_depth_max {0};

    std::chrono::steady_clock::time_point window_start;
    bool window_started {false};

    void reset(const std::chrono::steady_clock::time_point& now)
    {
        arrivals = 0;
        pts_delta_sum = 0.0;
        pts_delta_min = 0;
        pts_delta_max = 0;
        jitter_sum = 0.0;
        jitter_max = 0;
        nonmono = 0;
        overflow_drops = 0;
        presents = 0;
        late_presents = 0;
        lateness_sum = 0.0;
        lateness_max = 0;
        skips = 0;
        reanchors = 0;
        drain_slews = 0;
        retard_slews = 0;
        q_depth_max = 0;
        window_start = now;
    }
};

// Per-stream jitter buffer plus the clock anchor that maps frame pts onto the
// local steady clock. Frames are presented when their pts comes due against
// the anchor rather than on arrival, which smooths network/decode jitter.
struct stream_presentation final
{
    std::deque<frame> q;
    bool anchored {false};
    int64_t anchor_media_pts {0};
    std::chrono::steady_clock::time_point anchor_wall;
    // Learned latency target (ms of content to hold): raised by late presents,
    // decayed slowly when on time. Survives seeks (_reset_presentation) since
    // the stream's delivery jitter doesn't change with the playhead.
    double desired_buffer_ms {(double)PRESENTATION_CUSHION_MS};
    stream_stats stats;
};

class pipeline_state;

class pipeline_host final
{
public:
    pipeline_host(configure_state& cfg, SDL_Renderer* renderer);
    pipeline_host(const pipeline_host&) = delete;
    pipeline_host(pipeline_host&&) = delete;
    pipeline_host& operator=(const pipeline_host&) = delete;
    pipeline_host& operator=(pipeline_host&&) = delete;
    ~pipeline_host();

    void start();
    void stop();

    void change_layout(int window, layout l);

    void update_stream(int window, stream_info si);

    void disconnect_stream(int window, const std::string& name);

    void post_video_frame(const std::string& name, std::shared_ptr<std::vector<uint8_t>> buffer, uint16_t w, uint16_t h, uint16_t original_w, uint16_t original_h, int64_t pts);

    void set_active_audio_stream(const std::string& name);
    void set_stream_volume(const std::string& name, float gain);
    bool has_audio(const std::string& name) const;

    r_utils::r_nullable<std::shared_ptr<render_context>> lookup_render_context(const std::string& name, uint16_t w, uint16_t h);

    void control_bar_cb(const std::string& name, const std::chrono::system_clock::time_point& pos);
    void sync_control_bar_cb(const std::chrono::system_clock::time_point& pos);
    void sync_play_cb(const std::chrono::system_clock::time_point& range_end);
    void sync_live_cb();
    void control_bar_button_cb(const std::string& name, control_bar_button_type type);
    void control_bar_update_data_cb(const std::string& stream_name, control_bar_state& cbs);
    void control_bar_export_cb(const std::string& stream_name, const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end, control_bar_state& cbs);
    void control_bar_export_progress_cb(const std::string& stream_name, control_bar_state& cbs);

    void destroy_video_textures()
    {
        std::lock_guard<std::mutex> pipes_lock(_internals_lok);
        for(auto& rc : _render_contexts)
        {
            rc.second->tex.reset();
        }
    }

    // Called once per UI frame: picks the due frame (if any) for each stream
    // from its presentation queue and uploads it to that stream's texture.
    void load_video_textures();

    bool playing(const std::string& stream_name) const;
    std::chrono::system_clock::time_point last_control_bar_pos(const std::string& stream_name) const;

    // Returns true if new frames have arrived since last call (and clears the flag)
    bool consume_new_frames_flag()
    {
        return _has_new_frames.exchange(false);
    }

private:
    void _entry_point();

    // Drop queued frames and force a re-anchor; call on seek/play/live
    // transitions so stale frames can't present. Caller must hold _internals_lok.
    void _reset_presentation(const std::string& name)
    {
        auto it = _presentations.find(name);
        if(it != _presentations.end())
        {
            it->second.q.clear();
            it->second.anchored = false;
        }
    }

    mutable std::mutex _internals_lok;

    configure_state& _cfg;
    SDL_Renderer* _renderer;
    std::map<std::string, stream_info> _stream_infos;
    std::map<std::string, std::shared_ptr<pipeline_state>> _pipes;

    std::map<std::string, std::shared_ptr<render_context>> _render_contexts;
    std::map<std::string, stream_presentation> _presentations;
    std::map<std::string, std::string> _name_remap; // pipeline's internal name → current layout name

    // Playback tracking for relative timestamp calculation
    std::map<std::string, std::chrono::system_clock::time_point> _playback_start_positions;
    std::map<std::string, int64_t> _playback_start_pts;

    std::thread _th;
    bool _running;
    std::chrono::steady_clock::time_point _last_dead_check;

    // Stored so the dead-check loop can retry collect_stream_info() if the
    // local server wasn't ready when change_layout() was first called.
    int _retry_window{-1};
    layout _retry_layout{LAYOUT_ONE_BY_ONE};

    // Flag to signal main loop that new frames are available
    std::atomic<bool> _has_new_frames{false};

    // Flag to indicate camera list has been validated - don't connect until this is true
    bool _cameras_validated{false};

    std::string _active_audio_stream;

public:
    // Call after camera list validation to enable connections
    void set_cameras_validated()
    {
        std::lock_guard<std::mutex> g(_internals_lok);
        _cameras_validated = true;
    }

    // Check if cameras have been validated
    bool cameras_validated() const
    {
        std::lock_guard<std::mutex> g(_internals_lok);
        return _cameras_validated;
    }
};

}

#endif
