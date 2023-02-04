
#include "r_utils/r_exception.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_socket.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "pipeline_host.h"
#include "pipeline_state.h"
#include "utils.h"
#include "query.h"

using namespace vision;
using namespace r_pipeline;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

pipeline_host::pipeline_host(configure_state& cfg) :
    _cfg(cfg),
    _stream_infos(),
    _pipes(),
    _video_frame_buffer_lok(),
    _render_contexts(),
    _video_frames(),
    _th(),
    _running(false),
    _work_q(),
    _last_dead_check(steady_clock::now()),
    _last_stream_start(steady_clock::now())
{
}

pipeline_host::~pipeline_host()
{
    stop();

    _pipes.clear();
}

void pipeline_host::start()
{
    _th = thread(&pipeline_host::_entry_point, this);
}

void pipeline_host::stop()
{
    if(_running)
    {
        _running = false;
        _work_q.wake();
        _th.join();
    }
}

void pipeline_host::change_layout(int window, layout l)
{
    pipeline_host_cmd cmd;
    cmd.type = phc_change_layout;
    cmd.window.set_value(window);
    cmd.l.set_value(l);

    auto fr = _work_q.post(cmd);
    fr.wait();
}

void pipeline_host::update_stream(int window, stream_info si)
{
    pipeline_host_cmd cmd;
    cmd.type = phc_update_stream;
    cmd.window.set_value(window);
    cmd.si.set_value(si);

    auto fr = _work_q.post(cmd);
    fr.wait();
}

void pipeline_host::disconnect_stream(int window, const string& name)
{
    pipeline_host_cmd cmd;
    cmd.type = phc_disconnect_stream;
    cmd.window.set_value(window);
    cmd.name.set_value(name);

    auto fr = _work_q.post(cmd);
    fr.wait();
}

void pipeline_host::post_video_frame(const string& name, shared_ptr<vector<uint8_t>> buffer, uint16_t w, uint16_t h, uint16_t original_w, uint16_t original_h)
{
    lock_guard<mutex> g(_video_frame_buffer_lok);

    frame f;
    f.buffer = buffer;
    f.w = w;
    f.h = h;
    f.original_w = original_w;
    f.original_h = original_h;

    // If something goes into our frame buffer, then wakeup the main loop... but since
    // this queue is drained each time through the loop we only need to do it once...
    if(_video_frames.empty())
        glfwPostEmptyEvent();

    _video_frames.insert(make_pair(name, f));
}

r_nullable<shared_ptr<render_context>> pipeline_host::lookup_render_context(const std::string& name, uint16_t w, uint16_t h)
{
    lock_guard<mutex> g(_video_frame_buffer_lok);
    r_nullable<shared_ptr<render_context>> rc;

    bool resized = false;

    // First lookup in _stream_infos to see if this name should have a running pipeline...
    auto found_si = _stream_infos.find(name);
    if(found_si != end(_stream_infos))
    {
        // We should have a pipeline for this name, if we don't then create a pipeline state and start
        // it. If we DO have a pipeline_state but it's w X h don't match this w X h then call resize on it
        // with the new w X h.
        auto found_ps = _pipes.find(name);
        if(found_ps == end(_pipes))
        {
            auto ps = make_shared<pipeline_state>(found_si->second, this, w, h, _cfg);
            ps->play();
            _pipes.insert(make_pair(name, ps));
        }
        else
        {
            if(found_ps->second->width() != w || found_ps->second->height() != h)
            {
                resized = true;
                found_ps->second->resize(w, h);               
            }
        }
    }
 
    // Finally, once a buffer makes it to the end of the a pipeline we will create a render_context for it
    // If we have then get the texture id and return it.
    auto found_rc = _render_contexts.find(name);
    if(found_rc != end(_render_contexts))
    {
        if(resized)
        {
            found_rc->second->w = w;
            found_rc->second->h = h;
        }
        rc.set_value(found_rc->second);
    }
    return rc;
}

void pipeline_host::control_bar_cb(const string& name, const std::chrono::system_clock::time_point& pos)
{
    pipeline_host_cmd cmd;
    cmd.type = phc_control_bar_cb;
    cmd.name.set_value(name);
    cmd.control_bar_pos.set_value(pos);

    auto fr = _work_q.post(cmd);
    fr.wait();
}

void pipeline_host::control_bar_button_cb(const string& name, control_bar_button_type type)
{
    pipeline_host_cmd cmd;
    cmd.type = phc_control_bar_button_cb;
    cmd.name.set_value(name);
    cmd.button_type.set_value(type);

    auto fr = _work_q.post(cmd);
    fr.wait();
}

void pipeline_host::control_bar_update_data_cb(control_bar_state& cbs)
{
    pipeline_host_cmd cmd;
    cmd.type = phc_control_bar_update_data_cb;
    cmd.cbs.set_value(&cbs);

    auto fr = _work_q.post(cmd);
    fr.wait();
}

void pipeline_host::control_bar_export_cb(const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end, control_bar_state& cbs)
{
    pipeline_host_cmd cmd;
    cmd.type = phc_control_bar_export_cb;
    cmd.export_start.set_value(start);
    cmd.export_end.set_value(end);
    cmd.cbs.set_value(&cbs);

    auto fr = _work_q.post(cmd);
    fr.wait();
}

void pipeline_host::_entry_point()
{
    _running = true;
    while(_running)
    {
        auto maybe_crp = _work_q.poll(std::chrono::milliseconds(200));

        if(!maybe_crp.is_null())
        {
            pipeline_host_cmd_result result;

            if(maybe_crp.raw().first.type == phc_change_layout)
            {
                result = _change_layout(maybe_crp.raw().first.window.value(), maybe_crp.raw().first.l.value());
            }
            else if(maybe_crp.raw().first.type == phc_update_stream)
            {
                result = _update_stream(maybe_crp.raw().first.window.value(), maybe_crp.raw().first.si.value());
            }
            else if(maybe_crp.raw().first.type == phc_disconnect_stream)
            {
                result = _disconnect_stream(maybe_crp.raw().first.window.value(), maybe_crp.raw().first.name.value());
            }
            else if(maybe_crp.raw().first.type == phc_control_bar_cb)
            {
                result = _control_bar_cb(maybe_crp.raw().first.name.value(), maybe_crp.raw().first.control_bar_pos.value());
            }
            else if(maybe_crp.raw().first.type == phc_control_bar_button_cb)
            {
                result = _control_bar_button_cb(maybe_crp.raw().first.name.value(), maybe_crp.raw().first.button_type.value());
            }
            else if(maybe_crp.raw().first.type == phc_control_bar_update_data_cb)
            {
                result = _control_bar_update_data_cb(maybe_crp.raw().first.cbs.value());
            }
            else if(maybe_crp.raw().first.type == phc_control_bar_export_cb)
            {
                result = _control_bar_export_cb(maybe_crp.raw().first.cbs.value(), maybe_crp.raw().first.export_start.value(), maybe_crp.raw().first.export_end.value());
            }
            else
            {
                R_LOG_ERROR("Unknown command in pipeline_host!");
                result.success = false;
            }

            maybe_crp.raw().second.set_value(result);
        }

        auto now = steady_clock::now();

        // dead check
        if(duration_cast<seconds>(now - _last_dead_check) > seconds(10))
        {
            _last_dead_check = now;

            auto curr = begin(_pipes);
            while(curr != end(_pipes))
            {
                bool found_dead = false;

                if(curr->second->running() && curr->second->last_v_pts() == curr->second->v_pts_at_check())
                    found_dead = true;

                if(curr->second->running() && curr->second->has_audio() && curr->second->last_a_pts() == curr->second->a_pts_at_check())
                    found_dead = true;

                if(found_dead)
                {
                    R_LOG_ERROR("Dead stream detected");
                    curr->second->stop();
                    curr = _pipes.erase(curr);
                }
                else
                {
                    curr->second->set_v_pts_at_check(curr->second->last_v_pts());
                    curr->second->set_a_pts_at_check(curr->second->last_a_pts());
                    ++curr;
                }
            }
        }
    }
}

pipeline_host_cmd_result pipeline_host::_change_layout(int window, layout l)
{
    _stream_infos.clear();
    _pipes.clear();
    _render_contexts.clear();
    _video_frames.clear();

    auto sis = _cfg.collect_stream_info(window, l);

    for(auto si : sis)
        _stream_infos.insert(make_pair(si.name, si));

    pipeline_host_cmd_result result;
    result.success = true;
    return result;
}

pipeline_host_cmd_result pipeline_host::_update_stream(int window, stream_info si)
{
    _pipes.erase(si.name);
    _render_contexts.erase(si.name);
    _stream_infos[si.name] = si;

    pipeline_host_cmd_result result;
    result.success = true;
    return result;
}

pipeline_host_cmd_result pipeline_host::_disconnect_stream(int window, const string& name)
{
    _pipes.erase(name);
    _render_contexts[name]->done = true;
    _stream_infos.erase(name);

    pipeline_host_cmd_result result;
    result.success = true;
    return result;
}

pipeline_host_cmd_result pipeline_host::_control_bar_cb(const string& name, const std::chrono::system_clock::time_point& pos)
{
    auto found = _pipes.find(name);
    if(found != end(_pipes))
    {
        auto pipe = found->second;

        if(pipe->running())
            pipe->stop();

        pipe->control_bar(pos);
    }

    pipeline_host_cmd_result result;
    result.success = true;
    return result;
}

pipeline_host_cmd_result pipeline_host::_control_bar_button_cb(const string& name, control_bar_button_type button_type)
{
    auto found = _pipes.find(name);
    if(found != end(_pipes))
    {
        auto pipe = found->second;

        if(button_type == CONTROL_BAR_BUTTON_LIVE)
        {
            if(!pipe->running())
                pipe->play();
        }
    }

    pipeline_host_cmd_result result;
    result.success = true;
    return result;
}

pipeline_host_cmd_result pipeline_host::_control_bar_update_data_cb(control_bar_state* cbs)
{
    auto found_si = _stream_infos.find("0_onebyone_0");
    if(found_si != end(_stream_infos))
    {
        try
        {
            auto range = cbs->get_range();
            cbs->set_contents(query_segments(_cfg, found_si->second.camera_id, range.first, range.second));
            cbs->set_motion_events(query_motion_events(_cfg, found_si->second.camera_id, range.first, range.second));
        }
        catch(const std::exception& e)
        {
            R_LOG_EXCEPTION(e);
        }        
    }

    pipeline_host_cmd_result result;
    result.success = true;
    return result;
}

pipeline_host_cmd_result pipeline_host::_control_bar_export_cb(control_bar_state* cbs, const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end)
{
    auto found_si = _stream_infos.find("0_onebyone_0");
    if(found_si != std::end(_stream_infos))
    {
        auto tmt = system_clock::to_time_t(start);
        struct tm* bdt = localtime(&tmt);

        auto file_name = r_string_utils::format(
            "%04d-%02d-%02d_%02d-%02d-%02d.mov",
            bdt->tm_year + 1900,
            bdt->tm_mon + 1,
            bdt->tm_mday,
            bdt->tm_hour,
            bdt->tm_min,
            bdt->tm_sec
        );

        r_socket sok;
        sok.connect("127.0.0.1", 10080);

        r_http::r_client_request req("127.0.0.1", 10080);
        req.set_uri(
            r_string_utils::format(
                "/export?camera_id=%s&start_time=%s&end_time=%s&file_name=%s",
                found_si->second.camera_id.c_str(),
                r_time_utils::tp_to_iso_8601(start, false).c_str(),
                r_time_utils::tp_to_iso_8601(end, false).c_str(),
                file_name.c_str()
            )
        );
        req.write_request(sok);

        r_http::r_client_response res;
        res.read_response(sok);

        if(res.is_success())
        {
            cbs->exp_state = EXPORT_STATE_FINISHED_SUCCESS;
            R_LOG_INFO("Export finished successfully.");
            fflush(stdout);
        }
        else
        {
            cbs->exp_state = EXPORT_STATE_FINISHED_ERROR;
            R_LOG_INFO("Export finished with error.");
            fflush(stdout);
        }

    }

    pipeline_host_cmd_result result;
    result.success = true;
    return result;
}
