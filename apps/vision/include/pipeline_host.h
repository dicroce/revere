
#ifndef __vision_pipeline_host_h
#define __vision_pipeline_host_h

#include "r_utils/r_nullable.h"
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#ifdef IS_WINDOWS
#include <windows.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers
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
    pipeline_host(configure_state& cfg);
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
            glDeleteTextures(1, &rc.second->texture_id);
            rc.second->texture_id = 0;
        }
    }

    void load_video_textures()
    {
        std::lock_guard<std::mutex> pipes_lock(_internals_lok);

        for(auto& frame_p : _video_frames)
        {
            auto found_rc = _render_contexts.find(frame_p.first);
            if(found_rc == end(_render_contexts))
            {
                auto rc = std::make_shared<render_context>();
                glGenTextures(1, &rc->texture_id);
                rc->w = frame_p.second.w;
                rc->h = frame_p.second.h;
                _render_contexts.insert(make_pair(frame_p.first, rc));
            }
            else
            {
                if(found_rc->second->w != frame_p.second.w || found_rc->second->h != frame_p.second.h)
                {
                    glDeleteTextures(1, &found_rc->second->texture_id);
                    found_rc->second->texture_id = 0;
                    auto rc = std::make_shared<render_context>();
                    glGenTextures(1, &rc->texture_id);
                    rc->w = frame_p.second.w;
                    rc->h = frame_p.second.h;
                    _render_contexts[found_rc->first] = rc;
                }
            }

            auto rc = _render_contexts[frame_p.first];

            glBindTexture(GL_TEXTURE_2D, rc->texture_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#if defined(IS_LINUX) || defined(IS_MACOS)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#endif
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frame_p.second.w, frame_p.second.h, 0, GL_RGB, GL_UNSIGNED_BYTE, frame_p.second.buffer->data());
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

private:
    void _entry_point();

    mutable std::mutex _internals_lok;

    configure_state& _cfg;
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
};

}

#endif
