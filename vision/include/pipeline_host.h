
#ifndef __vision_pipeline_host_h
#define __vision_pipeline_host_h

#include "r_utils/r_work_q.h"
#include "r_utils/r_nullable.h"
#include <string>
#include <map>
#include <memory>
#include <mutex>
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

enum phc_type
{
    phc_change_layout,
    phc_update_stream,
    phc_disconnect_stream,
    phc_control_bar_cb,
    phc_control_bar_button_cb,
    phc_control_bar_update_data_cb,
    phc_control_bar_export_cb
};

struct pipeline_host_cmd
{
    phc_type type;
    r_utils::r_nullable<int> window;
    r_utils::r_nullable<vision::layout> l;
    r_utils::r_nullable<vision::stream_info> si;
    r_utils::r_nullable<std::string> name;
    r_utils::r_nullable<std::chrono::system_clock::time_point> control_bar_pos;
    r_utils::r_nullable<control_bar_button_type> button_type;
    r_utils::r_nullable<control_bar_state*> cbs;
    r_utils::r_nullable<std::chrono::system_clock::time_point> export_start;
    r_utils::r_nullable<std::chrono::system_clock::time_point> export_end;
};

struct pipeline_host_cmd_result
{
    bool success;
};

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

    void post_video_frame(const std::string& name, std::shared_ptr<std::vector<uint8_t>> buffer, uint16_t w, uint16_t h, uint16_t original_w, uint16_t original_h);

    r_utils::r_nullable<std::shared_ptr<render_context>> lookup_render_context(const std::string& name, uint16_t w, uint16_t h);

    void control_bar_cb(const std::string& name, const std::chrono::system_clock::time_point& pos);
    void control_bar_button_cb(const std::string& name, control_bar_button_type type);
    void control_bar_update_data_cb(control_bar_state& cbs);
    void control_bar_export_cb(const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end, control_bar_state& cbs);

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
        std::lock_guard<std::mutex> g(_video_frame_buffer_lok);

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
#ifdef IS_LINUX
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

    bool playing() const;

private:
    void _entry_point();
    pipeline_host_cmd_result _change_layout(int window, layout l);
    pipeline_host_cmd_result _update_stream(int window, stream_info si);
    pipeline_host_cmd_result _disconnect_stream(int window, const std::string& name);
    pipeline_host_cmd_result _control_bar_cb(const std::string& name, const std::chrono::system_clock::time_point& pos);
    pipeline_host_cmd_result _control_bar_button_cb(const std::string& name, control_bar_button_type button_type);
    pipeline_host_cmd_result _control_bar_update_data_cb(control_bar_state* cbs);
    pipeline_host_cmd_result _control_bar_export_cb(control_bar_state* cbs, const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end);

    configure_state& _cfg;
    std::map<std::string, stream_info> _stream_infos;
    std::map<std::string, std::shared_ptr<pipeline_state>> _pipes;

    std::mutex _video_frame_buffer_lok;
    std::map<std::string, std::shared_ptr<render_context>> _render_contexts;
    std::map<std::string, frame> _video_frames;

    std::thread _th;
    bool _running;
    r_utils::r_work_q<pipeline_host_cmd, pipeline_host_cmd_result> _work_q;
    std::chrono::steady_clock::time_point _last_dead_check;
    std::chrono::steady_clock::time_point _last_stream_start;
};

}

#endif
