
#ifndef __vision_pipeline_host_h
#define __vision_pipeline_host_h

#include "r_utils/r_nullable.h"
#include <string>
#include <map>
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

    r_utils::r_nullable<std::shared_ptr<render_context>> lookup_render_context(const std::string& name, uint16_t w, uint16_t h);

    void update_render_context_timestamp(const std::string& name, int64_t pts);

    void control_bar_cb(const std::string& name, const std::chrono::system_clock::time_point& pos);
    void control_bar_button_cb(const std::string& name, control_bar_button_type type);
    void control_bar_update_data_cb(const std::string& stream_name, control_bar_state& cbs);
    void control_bar_export_cb(const std::string& stream_name, const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end, control_bar_state& cbs);

    void destroy_video_textures()
    {
        for(auto& rc : _render_contexts)
        {
            rc.second->tex.reset();
        }
    }

    void load_video_textures()
    {
        std::lock_guard<std::mutex> pipes_lock(_internals_lok);

        static int load_log_count = 0;
        if (!_video_frames.empty() && load_log_count++ < 5)
        {
            R_LOG_INFO("load_video_textures: processing %zu video frames", _video_frames.size());
        }

        for(auto& frame_p : _video_frames)
        {
            auto found_rc = _render_contexts.find(frame_p.first);
            if(found_rc == end(_render_contexts))
            {
                static int create_log_count = 0;
                if (create_log_count++ < 5)
                {
                    R_LOG_INFO("Creating new render context for stream: %s (%dx%d)",
                        frame_p.first.c_str(), frame_p.second.w, frame_p.second.h);
                }

                // Create streaming texture for video (optimized for frequent updates)
                auto rc = std::make_shared<render_context>();
                rc->tex = r_ui_utils::texture::create_streaming(
                    _renderer,
                    frame_p.second.w,
                    frame_p.second.h,
                    false  // RGB, not RGBA
                );
                if (rc->tex)
                {
                    rc->tex->update_rgb(
                        frame_p.second.buffer->data(),
                        frame_p.second.w,
                        frame_p.second.h
                    );
                }
                else
                {
                    R_LOG_ERROR("Failed to create streaming texture for stream: %s", frame_p.first.c_str());
                }
                rc->w = frame_p.second.w;
                rc->h = frame_p.second.h;
                _render_contexts.insert(make_pair(frame_p.first, rc));
            }
            else
            {
                if(found_rc->second->w != frame_p.second.w || found_rc->second->h != frame_p.second.h)
                {
                    // Recreate streaming texture with new dimensions
                    auto rc = std::make_shared<render_context>();
                    rc->tex = r_ui_utils::texture::create_streaming(
                        _renderer,
                        frame_p.second.w,
                        frame_p.second.h,
                        false  // RGB, not RGBA
                    );
                    if (rc->tex)
                    {
                        rc->tex->update_rgb(
                            frame_p.second.buffer->data(),
                            frame_p.second.w,
                            frame_p.second.h
                        );
                    }
                    rc->w = frame_p.second.w;
                    rc->h = frame_p.second.h;
                    _render_contexts[found_rc->first] = rc;
                }
                else
                {
                    // Update existing streaming texture
                    if (found_rc->second->tex)
                    {
                        found_rc->second->tex->update_rgb(
                            frame_p.second.buffer->data(),
                            frame_p.second.w,
                            frame_p.second.h
                        );
                    }
                }
            }
        }

        _video_frames.clear();

        for(auto iter = begin(_render_contexts); iter != end(_render_contexts);)
        {
            if(iter->second->done)
                iter = _render_contexts.erase(iter);
            else ++iter;
        }
    }

    bool playing(const std::string& stream_name) const;

    // Returns true if new frames have arrived since last call (and clears the flag)
    bool consume_new_frames_flag()
    {
        return _has_new_frames.exchange(false);
    }

private:
    void _entry_point();

    mutable std::mutex _internals_lok;

    configure_state& _cfg;
    SDL_Renderer* _renderer;
    std::map<std::string, stream_info> _stream_infos;
    std::map<std::string, std::shared_ptr<pipeline_state>> _pipes;

    std::map<std::string, std::shared_ptr<render_context>> _render_contexts;
    std::map<std::string, frame> _video_frames;

    // Playback tracking for relative timestamp calculation
    std::map<std::string, std::chrono::system_clock::time_point> _playback_start_positions;
    std::map<std::string, int64_t> _playback_start_pts;

    std::thread _th;
    bool _running;
    std::chrono::steady_clock::time_point _last_dead_check;
    std::chrono::steady_clock::time_point _last_stream_start;

    // Flag to signal main loop that new frames are available
    std::atomic<bool> _has_new_frames{false};

    // Flag to indicate camera list has been validated - don't connect until this is true
    bool _cameras_validated{false};

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
