
#include "r_utils/r_exception.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_socket.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "pipeline_host.h"
#include "pipeline_state.h"
#include "utils.h"
#include "query.h"
#include "error_handling.h"

using namespace vision;
using namespace r_pipeline;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

pipeline_host::pipeline_host(configure_state& cfg) :
    _internals_lok(),
    _cfg(cfg),
    _stream_infos(),
    _pipes(),
    _render_contexts(),
    _video_frames(),
    _playback_start_positions(),
    _playback_start_pts(),
    _th(),
    _running(false),
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
        _th.join();
    }
}

void pipeline_host::change_layout(int window, layout l)
{
    lock_guard<mutex> g(_internals_lok);

    _stream_infos.clear();
    _pipes.clear();
    _render_contexts.clear();
    _video_frames.clear();

    auto sis = _cfg.collect_stream_info(window, l);

    for(auto si : sis)
        _stream_infos.insert(make_pair(si.name, si));
}

void pipeline_host::update_stream(int window, stream_info si)
{
    // DEADLOCK FIX: Extract the old pipeline before destroying it
    // Problem: pipeline_state destructor calls thread.join() which waits for the
    // pipeline thread to exit. But the pipeline thread calls post_video_frame()
    // which tries to acquire _internals_lok. If we destroy the pipeline while
    // holding the lock, we get a deadlock:
    // - Main thread: holds lock, waits for pipeline thread to exit
    // - Pipeline thread: waits for lock to call post_video_frame()
    shared_ptr<pipeline_state> old_pipeline;
    {
        lock_guard<mutex> g(_internals_lok);
        
        // Extract the existing pipeline (if any) before erasing from map
        auto pipe_it = _pipes.find(si.name);
        if (pipe_it != _pipes.end()) {
            old_pipeline = pipe_it->second;  // Keep reference to prevent immediate destruction
            _pipes.erase(pipe_it);           // Remove from map but don't destroy yet
        }
        
        _render_contexts.erase(si.name);
        _stream_infos[si.name] = si;
    }  // Lock is released here
    
    // Destroy the old pipeline outside the critical section
    // Now the pipeline thread can complete post_video_frame() calls and exit cleanly
    // before thread.join() is called in the pipeline_state destructor
    old_pipeline.reset();
}

void pipeline_host::disconnect_stream(int window, const string& name)
{
    // DEADLOCK FIX: Same pattern as update_stream() to avoid deadlock
    // when destroying pipeline threads that may be calling post_video_frame()
    shared_ptr<pipeline_state> old_pipeline;
    {
        lock_guard<mutex> g(_internals_lok);

        // Extract the pipeline before destroying it
        auto pipe_it = _pipes.find(name);
        if (pipe_it != _pipes.end()) {
            old_pipeline = pipe_it->second;  // Keep reference to prevent immediate destruction
            _pipes.erase(pipe_it);           // Remove from map but don't destroy yet
        }
        
        // Signal render context to stop and clean up
        auto rc_it = _render_contexts.find(name);
        if (rc_it != _render_contexts.end()) {
            rc_it->second->done = true;      // Signal rendering to stop
            _render_contexts.erase(rc_it);
        }
        
        _stream_infos.erase(name);
    }  // Lock is released here
    
    // Destroy the old pipeline outside the critical section
    // Prevents deadlock between main thread and pipeline worker thread
    old_pipeline.reset();
}

void pipeline_host::post_video_frame(const string& name, shared_ptr<vector<uint8_t>> buffer, uint16_t w, uint16_t h, uint16_t original_w, uint16_t original_h, int64_t pts)
{
    lock_guard<mutex> g(_internals_lok);

    // Input validation
    if (name.empty())
    {
        R_LOG_ERROR("Empty stream name in post_video_frame");
        return;
    }
    
    if (!buffer)
    {
        R_LOG_ERROR("NULL buffer in post_video_frame for stream %s", name.c_str());
        return;
    }
    
    // Validate frame dimensions
    if (!state_validate::is_valid_frame_dimensions(w, h))
    {
        R_LOG_ERROR("Invalid frame dimensions in post_video_frame for stream %s: %dx%d", name.c_str(), w, h);
        return;
    }
    
    if (!state_validate::is_valid_frame_dimensions(original_w, original_h))
    {
        R_LOG_ERROR("Invalid original frame dimensions in post_video_frame for stream %s: %dx%d", name.c_str(), original_w, original_h);
        return;
    }
    
    // Validate buffer size (assuming RGB format = 3 channels)
    if (!state_validate::is_valid_buffer_size(buffer->size(), w, h, 3))
    {
        R_LOG_ERROR("Invalid buffer size in post_video_frame for stream %s: %zu bytes for %dx%d", name.c_str(), buffer->size(), w, h);
        return;
    }

    // Calculate playback-relative timestamp if we're in playback mode
    int64_t display_pts = pts;
    auto playback_start_pos_it = _playback_start_positions.find(name);
    
    if (playback_start_pos_it != _playback_start_positions.end())
    {
        auto playback_start_pts_it = _playback_start_pts.find(name);
        if (playback_start_pts_it != _playback_start_pts.end())
        {
            // We're in playback mode - calculate relative timestamp
            if (playback_start_pts_it->second == 0)
            {
                // First frame of playback - record the starting PTS
                _playback_start_pts[name] = pts;
                display_pts = std::chrono::duration_cast<std::chrono::milliseconds>(
                    playback_start_pos_it->second.time_since_epoch()).count();
            }
            else
            {
                // Subsequent frames - add elapsed time to playback start position
                int64_t elapsed_ms = pts - playback_start_pts_it->second; // PTS is already in milliseconds
                display_pts = std::chrono::duration_cast<std::chrono::milliseconds>(
                    playback_start_pos_it->second.time_since_epoch()).count() + elapsed_ms;
            }
        }
    }

    frame f;
    f.buffer = buffer;
    f.w = w;
    f.h = h;
    f.original_w = original_w;
    f.original_h = original_h;
    f.pts = display_pts;

    // If something goes into our frame buffer, then wakeup the main loop... but since
    // this queue is drained each time through the loop we only need to do it once...
    if(_video_frames.empty())
        glfwPostEmptyEvent();

    _video_frames.insert(make_pair(name, f));
    
    // Update render context timestamp if it exists
    auto found_rc = _render_contexts.find(name);
    if(found_rc != end(_render_contexts))
    {
        found_rc->second->pts = display_pts;
    }
}

r_nullable<shared_ptr<render_context>> pipeline_host::lookup_render_context(const std::string& name, uint16_t w, uint16_t h)
{
    lock_guard<mutex> pipes_lock(_internals_lok);

    // Input validation
    if (name.empty())
    {
        R_LOG_ERROR("Empty stream name in lookup_render_context");
        return r_nullable<shared_ptr<render_context>>();
    }
    
    // Validate frame dimensions
    if (!state_validate::is_valid_frame_dimensions(w, h))
    {
        R_LOG_ERROR("Invalid frame dimensions in lookup_render_context for stream %s: %dx%d", name.c_str(), w, h);
        return r_nullable<shared_ptr<render_context>>();
    }

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
            try
            {
                auto ps = make_shared<pipeline_state>(found_si->second, this, w, h, _cfg);
                ps->play_live();
                _pipes.insert(make_pair(name, ps));
            }
            catch(const std::exception& e)
            {
                R_LOG_ERROR("Failed to create pipeline for stream %s: %s", name.c_str(), e.what());
                return r_nullable<shared_ptr<render_context>>();
            }
        }
        else
        {
            if(found_ps->second->width() != w || found_ps->second->height() != h)
            {
                // Validate the new dimensions before resizing
                if (!state_validate::is_valid_frame_dimensions(w, h))
                {
                    R_LOG_ERROR("Invalid resize dimensions for stream %s: %dx%d", name.c_str(), w, h);
                    return r_nullable<shared_ptr<render_context>>();
                }
                
                try
                {
                    resized = true;
                    found_ps->second->resize(w, h);
                }
                catch(const std::exception& e)
                {
                    R_LOG_ERROR("Failed to resize pipeline for stream %s: %s", name.c_str(), e.what());
                    return r_nullable<shared_ptr<render_context>>();
                }
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
            // Validate dimensions before updating render context
            if (state_validate::is_valid_frame_dimensions(w, h))
            {
                found_rc->second->w = w;
                found_rc->second->h = h;
            }
            else
            {
                R_LOG_ERROR("Invalid dimensions for render context update: %dx%d", w, h);
                return r_nullable<shared_ptr<render_context>>();
            }
        }
        rc.set_value(found_rc->second);
    }
    return rc;
}

void pipeline_host::update_render_context_timestamp(const std::string& name, int64_t pts)
{
    lock_guard<mutex> g(_internals_lok);
    
    auto found_rc = _render_contexts.find(name);
    if(found_rc != end(_render_contexts))
    {
        found_rc->second->pts = pts;
    }
}

void pipeline_host::control_bar_cb(const string& name, const std::chrono::system_clock::time_point& pos)
{
    lock_guard<mutex> pipes_lock(_internals_lok);
    auto found = _pipes.find(name);
    if(found != end(_pipes))
    {
        auto pipe = found->second;

        if(pipe->running())
            pipe->stop();

        // Clear playback tracking when user drags slider
        _playback_start_positions.erase(name);
        _playback_start_pts.erase(name);

        pipe->control_bar(pos);
    }
}

void pipeline_host::control_bar_button_cb(const string& name, control_bar_button_type type)
{
    lock_guard<mutex> pipes_lock(_internals_lok);
    auto found = _pipes.find(name);
    if(found != end(_pipes))
    {
        auto pipe = found->second;

        if(type == CONTROL_BAR_BUTTON_LIVE)
        {
            if(!pipe->running())
            {
                // Clear playback tracking when going live
                _playback_start_positions.erase(name);
                _playback_start_pts.erase(name);
                pipe->play_live();
            }
        }
        else if(type == CONTROL_BAR_BUTTON_PLAY)
        {
            if(!pipe->running())
            {
                // Record the playback start position and reset PTS tracking
                _playback_start_positions[name] = pipe->get_last_control_bar_pos();
                _playback_start_pts[name] = 0; // Will be set when first frame arrives
                pipe->play();
            }
        }
    }
}

void pipeline_host::control_bar_update_data_cb(const std::string& stream_name, control_bar_state& cbs)
{
    lock_guard<mutex> pipes_lock(_internals_lok);
    auto found_pipe = _pipes.find(stream_name);

    auto found_si = _stream_infos.find(stream_name);
    if(found_si != end(_stream_infos) && found_pipe != end(_pipes))
    {
        try
        {
            auto range = cbs.get_range();
            found_pipe->second->update_range(range.first, range.second);
            cbs.set_contents(query_segments(_cfg, found_si->second.camera_id, range.first, range.second));

            // Query motion events
            auto motion_events = query_motion_events(_cfg, found_si->second.camera_id, range.first, range.second);
            cbs.set_motion_events(motion_events);

            // Query analytics events
            auto analytics_events = query_analytics(_cfg, found_si->second.camera_id, range.first, range.second);
            cbs.set_analytics_events(analytics_events);
        }
        catch(const std::exception& e)
        {
            R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
        }
    }
    else
    {
        R_LOG_ERROR("Stream info or pipe not found for %s", stream_name.c_str());
    }
}

void pipeline_host::control_bar_export_cb(const std::string& stream_name, const std::chrono::system_clock::time_point& start, const std::chrono::system_clock::time_point& end, control_bar_state& cbs)
{
    lock_guard<mutex> pipes_lock(_internals_lok);

    auto found_si = _stream_infos.find(stream_name);
    if(found_si != std::end(_stream_infos))
    {
        auto tmt = system_clock::to_time_t(start);

#ifdef IS_WINDOWS
        struct tm bdt;
        struct tm* bdtp = nullptr;
        localtime_s(&bdt, &tmt);
        bdtp = &bdt;
#else
        struct tm* bdtp = localtime(&tmt);
#endif

        auto file_name = r_string_utils::format(
            "%04d-%02d-%02d_%02d-%02d-%02d.mov",
            bdtp->tm_year + 1900,
            bdtp->tm_mon + 1,
            bdtp->tm_mday,
            bdtp->tm_hour,
            bdtp->tm_min,
            bdtp->tm_sec
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
            cbs.exp_state = EXPORT_STATE_FINISHED_SUCCESS;
            R_LOG_INFO("Export finished successfully.");
            fflush(stdout);
        }
        else
        {
            cbs.exp_state = EXPORT_STATE_FINISHED_ERROR;
            R_LOG_INFO("Export finished with error.");
            fflush(stdout);
        }
    }
}

bool pipeline_host::playing(const std::string& stream_name) const
{
    lock_guard<mutex> pipes_lock(_internals_lok);
    auto found_pipe = _pipes.find(stream_name);

    auto found_si = _stream_infos.find(stream_name);
    if(found_si != end(_stream_infos) && found_pipe != end(_pipes))
        return found_pipe->second->playing();

    return false;
}

void pipeline_host::_entry_point()
{
    _running = true;
    while(_running)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if(!_running)
            continue;

        auto now = steady_clock::now();

        // dead check
        if(duration_cast<seconds>(now - _last_dead_check) > seconds(10))
        {
            _last_dead_check = now;

            lock_guard<mutex> pipes_lock(_internals_lok);
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
