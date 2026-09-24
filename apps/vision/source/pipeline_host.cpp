
#include "r_utils/r_exception.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_socket.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include <SDL.h>
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
using json = nlohmann::json;

pipeline_host::pipeline_host(configure_state& cfg, SDL_Renderer* renderer) :
    _internals_lok(),
    _cfg(cfg),
    _renderer(renderer),
    _stream_infos(),
    _pipes(),
    _render_contexts(),
    _presentations(),
    _playback_start_positions(),
    _playback_start_pts(),
    _th(),
    _running(false),
    _last_dead_check(steady_clock::now())
{
}

pipeline_host::~pipeline_host()
{
    stop();

    // DEADLOCK FIX: Extract pipes before destroying them to avoid deadlock
    // Same pattern as update_stream() and disconnect_stream()
    std::map<std::string, std::shared_ptr<pipeline_state>> old_pipes;
    {
        lock_guard<mutex> g(_internals_lok);
        old_pipes = std::move(_pipes);
        _pipes.clear();
    }  // Lock is released here

    // Destroy pipelines outside the critical section
    old_pipes.clear();
}

void pipeline_host::start()
{
    _th = thread(&pipeline_host::_entry_point, this);
    _timeline_running = true;
    _timeline_th = thread(&pipeline_host::_timeline_entry_point, this);
}

void pipeline_host::stop()
{
    if(_running)
    {
        _running = false;
        _th.join();
    }
    if(_timeline_running)
    {
        {
            lock_guard<mutex> g(_timeline_lok);
            _timeline_running = false;
        }
        _timeline_cv.notify_all();
        _timeline_th.join();
    }
}

void pipeline_host::change_layout(int window, layout l)
{
    // DEADLOCK FIX: Extract pipes before destroying them
    std::map<std::string, std::shared_ptr<pipeline_state>> old_pipes;
    {
        lock_guard<mutex> g(_internals_lok);

        auto sis = _cfg.collect_stream_info(window, l);

        // Build camera_id → (posting_name, pipeline) for all currently running pipes.
        // posting_name is _si.name inside the pipeline — what it passes to post_video_frame.
        std::map<std::string, std::pair<std::string, std::shared_ptr<pipeline_state>>> camera_to_pipe;
        for(auto& [map_name, si] : _stream_infos)
        {
            auto pipe_it = _pipes.find(map_name);
            if(pipe_it != end(_pipes))
                camera_to_pipe[si.camera_id] = {pipe_it->second->name(), pipe_it->second};
        }

        // Determine which pipelines can be reused by matching camera_id.
        _name_remap.clear();
        std::map<std::string, std::shared_ptr<pipeline_state>> new_pipes;
        std::map<std::string, std::shared_ptr<render_context>> new_rc;
        std::map<std::string, stream_presentation> new_pres;
        std::set<std::string> reused_posting_names;

        for(auto& si : sis)
        {
            auto it = camera_to_pipe.find(si.camera_id);
            if(it != end(camera_to_pipe))
            {
                const auto& posting_name = it->second.first;
                reused_posting_names.insert(posting_name);
                new_pipes[si.name] = it->second.second;

                if(posting_name != si.name)
                    _name_remap[posting_name] = si.name;

                // Transfer render context and video frame under the new name.
                for(auto& [map_name, old_si] : _stream_infos)
                {
                    if(old_si.camera_id == si.camera_id)
                    {
                        auto rc_it = _render_contexts.find(map_name);
                        if(rc_it != end(_render_contexts))
                            new_rc[si.name] = rc_it->second;

                        auto pres_it = _presentations.find(map_name);
                        if(pres_it != end(_presentations))
                            new_pres[si.name] = std::move(pres_it->second);
                        break;
                    }
                }
            }
        }

        // Pipelines not being reused go into old_pipes for teardown outside the lock.
        for(auto& [map_name, si] : _stream_infos)
        {
            auto pipe_it = _pipes.find(map_name);
            if(pipe_it != end(_pipes) && !reused_posting_names.count(pipe_it->second->name()))
                old_pipes[map_name] = pipe_it->second;
        }

        _stream_infos.clear();
        for(auto& si : sis)
            _stream_infos[si.name] = si;

        _pipes = std::move(new_pipes);
        _render_contexts = std::move(new_rc);
        _presentations = std::move(new_pres);

        _retry_window = window;
        _retry_layout = l;
    }  // Lock is released here

    // Destroy old pipelines outside the critical section
    old_pipes.clear();
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
        _presentations.erase(si.name);
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

        _presentations.erase(name);
        _stream_infos.erase(name);
    }  // Lock is released here

    // Destroy the old pipeline outside the critical section
    // Prevents deadlock between main thread and pipeline worker thread
    old_pipeline.reset();
}

void pipeline_host::set_active_audio_stream(const string& name)
{
    lock_guard<mutex> g(_internals_lok);
    _active_audio_stream = name;
    for(auto& p : _pipes)
        p.second->set_audio_active(p.first == name);
}

void pipeline_host::set_stream_volume(const string& name, float gain)
{
    lock_guard<mutex> g(_internals_lok);
    auto it = _pipes.find(name);
    if(it != end(_pipes))
        it->second->set_volume_gain(gain);
}

bool pipeline_host::has_audio(const string& name) const
{
    lock_guard<mutex> g(_internals_lok);
    auto it = _pipes.find(name);
    return it != end(_pipes) && it->second->has_audio();
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
    
    // Validate buffer size (assuming BGRA format = 4 bytes per pixel)
    if (!state_validate::is_valid_buffer_size(buffer->size(), w, h, 4))
    {
        R_LOG_ERROR("Invalid buffer size in post_video_frame for stream %s: %zu bytes for %dx%d", name.c_str(), buffer->size(), w, h);
        return;
    }

    // Translate the pipeline's internal name to the current layout name if it was reused.
    auto remap_it = _name_remap.find(name);
    const string& effective_name = (remap_it != end(_name_remap)) ? remap_it->second : name;

    // Debug: Log frame reception with pixel sample
    static int frame_log_count = 0;
    if (frame_log_count++ < 5)
    {
        const uint8_t* data = buffer->data();
        R_LOG_INFO("post_video_frame: stream=%s, size=%dx%d, buffer_size=%zu, first_pixels: B=%d G=%d R=%d A=%d",
            name.c_str(), w, h, buffer->size(), data[0], data[1], data[2], data[3]);
    }

    // Calculate playback-relative timestamp if we're in playback mode
    int64_t display_pts = pts;
    auto playback_start_pos_it = _playback_start_positions.find(effective_name);

    if (playback_start_pos_it != _playback_start_positions.end())
    {
        auto playback_start_pts_it = _playback_start_pts.find(effective_name);
        if (playback_start_pts_it != _playback_start_pts.end())
        {
            // We're in playback mode - calculate relative timestamp
            if (playback_start_pts_it->second == 0)
            {
                // First frame of playback - record the starting PTS
                _playback_start_pts[effective_name] = pts;
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

    // Signal that new frames are available
    _has_new_frames.store(true);

    auto& pres = _presentations[effective_name];

    // Arrival-side smoothness stats: pts_d measures the camera's timestamp
    // cadence; arr_jit measures how far delivery (network + encoder) deviates
    // from that cadence. Deltas outside (0, 5000) ms are discontinuities
    // (stills, seeks) and are excluded.
    {
        auto arrival_now = steady_clock::now();
        auto& st = pres.stats;
        if(!st.window_started)
        {
            st.window_started = true;
            st.window_start = arrival_now;
        }
        f.arrival = arrival_now;
        if(st.have_last_arrival)
        {
            int64_t pts_d = display_pts - st.last_arrival_pts;
            if(pts_d <= 0 && pts_d > -5000)
                st.nonmono++;
            if(pts_d > 0 && pts_d < 5000)
            {
                int64_t arr_d = duration_cast<milliseconds>(arrival_now - st.last_arrival_wall).count();
                int64_t jit = arr_d - pts_d;
                if(jit < 0)
                    jit = -jit;

                if(st.arrivals == 0 || pts_d < st.pts_delta_min)
                    st.pts_delta_min = pts_d;
                if(pts_d > st.pts_delta_max)
                    st.pts_delta_max = pts_d;
                st.pts_delta_sum += (double)pts_d;

                st.jitter_sum += (double)jit;
                if(jit > st.jitter_max)
                    st.jitter_max = jit;

                st.arrivals++;
            }
        }
        st.have_last_arrival = true;
        st.last_arrival_wall = arrival_now;
        st.last_arrival_pts = display_pts;
    }

    // If something goes into an empty queue, wake up the main loop.
    if(pres.q.empty())
    {
        SDL_Event event;
        event.type = SDL_USEREVENT;
        SDL_PushEvent(&event);
    }

    // Bounded jitter buffer: drop the oldest frame if arrival outruns
    // presentation. Note: rc->pts is set at presentation time (in
    // load_video_textures) so it always matches the displayed frame.
    if(pres.q.size() >= PRESENTATION_MAX_QUEUE)
    {
        pres.q.pop_front();
        pres.stats.overflow_drops++;
    }
    pres.q.push_back(std::move(f));
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

    // Don't try to connect until cameras have been validated against revere's camera list
    if (!_cameras_validated)
    {
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
                ps->set_audio_active(!_active_audio_stream.empty() && name == _active_audio_stream);
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

void pipeline_host::load_video_textures()
{
    // Phase 1 (locked): for each stream, decide which queued frame (if any) is
    // due for presentation and snapshot the render context pointer. All texture
    // work happens after the lock is released so decode threads posting frames
    // aren't stalled behind pixel uploads.
    struct upload_item
    {
        std::string name;
        frame f;
        std::shared_ptr<render_context> rc; // null → needs a new render context
    };
    std::vector<upload_item> uploads;
    std::vector<std::shared_ptr<render_context>> retired;
    std::vector<std::string> stats_lines; // built under the lock, logged after

    auto now = steady_clock::now();

    {
        lock_guard<mutex> g(_internals_lok);

        for(auto& [name, pres] : _presentations)
        {
            // Emit and reset the smoothness stats window, even for streams
            // with nothing currently queued.
            auto& st = pres.stats;
            if(st.window_started && now - st.window_start >= seconds(STATS_WINDOW_SECONDS))
            {
                if(st.arrivals > 0 || st.presents > 0)
                {
                    auto elapsed_s = duration_cast<seconds>(now - st.window_start).count();
                    stats_lines.push_back(r_string_utils::format(
                        "vision_stats[%s] %llds: arr=%u nmono=%u pts_d(ms) avg=%.1f min=%lld max=%lld | arr_jit(ms) avg=%.1f max=%lld | pres=%u late=%u lat(ms) avg=%.1f max=%lld | skip=%u drop=%u reanch=%u slew=%u rslew=%u qmax=%zu dbuf=%.0f",
                        name.c_str(),
                        (long long)elapsed_s,
                        st.arrivals,
                        st.nonmono,
                        st.pts_delta_sum / (double)((st.arrivals > 0) ? st.arrivals : 1),
                        (long long)st.pts_delta_min,
                        (long long)st.pts_delta_max,
                        st.jitter_sum / (double)((st.arrivals > 0) ? st.arrivals : 1),
                        (long long)st.jitter_max,
                        st.presents,
                        st.late_presents,
                        st.lateness_sum / (double)((st.presents > 0) ? st.presents : 1),
                        (long long)st.lateness_max,
                        st.skips,
                        st.overflow_drops,
                        st.reanchors,
                        st.drain_slews,
                        st.retard_slews,
                        st.q_depth_max,
                        pres.desired_buffer_ms));
                }
                st.reset(now);
            }

            if(pres.q.empty())
                continue;

            if(pres.q.size() > st.q_depth_max)
                st.q_depth_max = pres.q.size();

            int64_t target = 0;
            if(pres.anchored)
                target = pres.anchor_media_pts + duration_cast<milliseconds>(now - pres.anchor_wall).count();

            // First frame, seek, live↔playback switch, or still: re-anchor the
            // presentation clock. The front frame presents immediately and later
            // frames get the learned buffer target as cushion so arrival jitter
            // doesn't make them late.
            int64_t front_gap = pres.q.front().pts - target;
            if(!pres.anchored || front_gap > PRESENTATION_DISCONTINUITY_MS || front_gap < -PRESENTATION_DISCONTINUITY_MS)
            {
                if(pres.anchored)
                    st.reanchors++;
                pres.anchored = true;
                pres.anchor_media_pts = pres.q.front().pts;
                pres.anchor_wall = now + milliseconds((int64_t)pres.desired_buffer_ms);
                target = pres.q.front().pts;
            }

            // Present the newest due frame; older due frames are skipped.
            int due_idx = -1;
            for(size_t i = 0; i < pres.q.size(); ++i)
            {
                if(pres.q[i].pts <= target)
                    due_idx = (int)i;
                else
                    break;
            }

            if(due_idx < 0)
            {
                // Nothing due yet. If the queue has filled anyway, arrival is
                // outpacing our clock — pull the anchor back so we drain.
                if(pres.q.size() >= PRESENTATION_MAX_QUEUE)
                {
                    pres.anchor_wall -= milliseconds(5);
                    st.drain_slews++;
                }
                continue;
            }

            upload_item u;
            u.name = name;
            u.f = pres.q[due_idx];
            pres.q.erase(pres.q.begin(), pres.q.begin() + due_idx + 1);

            st.presents++;
            st.skips += (uint32_t)due_idx;
            int64_t lateness = target - u.f.pts;
            st.lateness_sum += (double)lateness;
            if(lateness > st.lateness_max)
                st.lateness_max = lateness;
            if(lateness > PRESENTATION_LATE_MS)
            {
                st.late_presents++;

                // Grow the buffer target only when the frame truly ARRIVED after
                // its due time — genuine under-buffering that more latency can
                // fix. Pathological timestamps (near-duplicate pts pairs,
                // reordering) also produce "late" presents, but no amount of
                // buffer fixes those, and growing on them ratcheted the target
                // to its cap. (An earlier version also drained on queue depth
                // >= 3, which contradicted any raised cushion — the two slews
                // fought in a limit cycle, skipping frames at every trough.)
                auto due_wall = pres.anchor_wall + milliseconds(u.f.pts - pres.anchor_media_pts);
                if(u.f.arrival > due_wall)
                {
                    auto bump = std::min(lateness, PRESENTATION_RETARD_STEP_MS);
                    pres.anchor_wall += milliseconds(bump);
                    pres.desired_buffer_ms = std::min(pres.desired_buffer_ms + (double)bump, PRESENTATION_MAX_DESIRED_BUFFER_MS);
                    st.retard_slews++;
                }
            }
            else
            {
                // On-time present: slowly reclaim latency in case the target
                // overshot or delivery has improved.
                pres.desired_buffer_ms = std::max((double)PRESENTATION_CUSHION_MS, pres.desired_buffer_ms - PRESENTATION_DESIRED_DECAY_MS);
            }

            // Proportional drain, only of buffer above the learned target
            // (also absorbs clock drift between the stream and the local clock).
            if(!pres.q.empty())
            {
                double buffered_ms = (double)(pres.q.back().pts - target);
                double excess = buffered_ms - pres.desired_buffer_ms;
                if(excess > 0.0)
                {
                    auto drain_us = (int64_t)(std::min(excess * PRESENTATION_DRAIN_GAIN, PRESENTATION_DRAIN_MAX_MS) * 1000.0);
                    pres.anchor_wall -= microseconds(drain_us);
                    st.drain_slews++;
                }
            }

            auto found_rc = _render_contexts.find(name);
            if(found_rc != end(_render_contexts))
                u.rc = found_rc->second;

            uploads.push_back(std::move(u));
        }

        for(auto iter = begin(_render_contexts); iter != end(_render_contexts);)
        {
            if(iter->second->done)
            {
                retired.push_back(iter->second);
                iter = _render_contexts.erase(iter);
            }
            else ++iter;
        }
    }

    for(const auto& line : stats_lines)
        R_LOG_INFO("%s", line.c_str());

    // Phase 2 (unlocked): create/update textures. Only this (UI) thread ever
    // touches texture objects, so no lock is needed for the pixel uploads.
    std::vector<std::pair<std::string, std::shared_ptr<render_context>>> new_rcs;
    for(auto& u : uploads)
    {
        if(!u.rc || u.rc->w != u.f.w || u.rc->h != u.f.h)
        {
            static int create_log_count = 0;
            if (create_log_count++ < 5)
            {
                R_LOG_INFO("Creating render context for stream: %s (%dx%d)",
                    u.name.c_str(), u.f.w, u.f.h);
            }

            // Create streaming texture for video (optimized for frequent updates)
            auto rc = std::make_shared<render_context>();
            rc->tex = r_ui_utils::texture::create_streaming(
                _renderer,
                u.f.w,
                u.f.h,
                false  // RGB, not RGBA
            );
            if (rc->tex)
                rc->tex->update_rgb(u.f.buffer->data(), u.f.w, u.f.h);
            else
                R_LOG_ERROR("Failed to create streaming texture for stream: %s", u.name.c_str());

            rc->w = u.f.w;
            rc->h = u.f.h;
            rc->pts = u.f.pts;
            new_rcs.emplace_back(u.name, rc);
        }
        else if(u.rc->tex)
        {
            u.rc->tex->update_rgb(u.f.buffer->data(), u.f.w, u.f.h);
            u.rc->pts = u.f.pts;
        }
    }

    // Phase 3 (locked): publish any new render contexts.
    if(!new_rcs.empty())
    {
        lock_guard<mutex> g(_internals_lok);
        for(auto& p : new_rcs)
            _render_contexts[p.first] = p.second;
    }

    // retired render contexts (and their textures) destroy here, outside the lock
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
        _reset_presentation(name);

        pipe->control_bar(pos);
    }
}

void pipeline_host::sync_control_bar_cb(const std::chrono::system_clock::time_point& pos)
{
    lock_guard<mutex> pipes_lock(_internals_lok);
    for(auto& [name, pipe] : _pipes)
    {
        if(pipe->running())
            pipe->stop();
        _playback_start_positions.erase(name);
        _playback_start_pts.erase(name);
        _reset_presentation(name);
        pipe->control_bar(pos);
    }
}

void pipeline_host::sync_play_cb(const std::chrono::system_clock::time_point& range_end)
{
    lock_guard<mutex> pipes_lock(_internals_lok);
    for(auto& [name, pipe] : _pipes)
    {
        if(!pipe->running())
        {
            pipe->update_range(pipe->get_last_control_bar_pos(), range_end);
            _playback_start_positions[name] = pipe->get_last_control_bar_pos();
            _playback_start_pts[name] = 0;
            _reset_presentation(name);
            pipe->play();
        }
    }
}

void pipeline_host::sync_live_cb()
{
    lock_guard<mutex> pipes_lock(_internals_lok);
    for(auto& [name, pipe] : _pipes)
    {
        if(pipe->running())
            pipe->stop();
        _playback_start_positions.erase(name);
        _playback_start_pts.erase(name);
        _reset_presentation(name);
        pipe->play_live();
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
                _reset_presentation(name);
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
                _reset_presentation(name);
                pipe->play();
            }
        }
    }
}

void pipeline_host::control_bar_update_data_cb(const std::string& stream_name, control_bar_state& cbs)
{
    // Runs on the render/UI thread and MUST NOT block. Resolve the stream and
    // push the new range into the pipe under the lock, then hand the timeline
    // queries to the background fetcher (_timeline_entry_point) and apply
    // whatever result is already available. The three HTTP round-trips used to
    // run inline here; a slow or exited revere then blocked the render loop for
    // up to their full timeout, freezing the window (fatally so for the
    // single-window overlay).
    auto range = cbs.get_range();
    std::string camera_id;
    {
        lock_guard<mutex> pipes_lock(_internals_lok);
        auto found_pipe = _pipes.find(stream_name);
        auto found_si = _stream_infos.find(stream_name);
        if(found_si == end(_stream_infos) || found_pipe == end(_pipes))
        {
            R_LOG_ERROR("Stream info or pipe not found for %s", stream_name.c_str());
            return;
        }
        found_pipe->second->update_range(range.first, range.second);
        camera_id = found_si->second.camera_id;
    }

    // Apply any completed fetch into cbs (cbs is owned by this UI thread), and
    // (re)post the latest wanted range for the worker to fetch. Both are quick,
    // in-memory operations under _timeline_lok — never any network here.
    {
        lock_guard<mutex> g(_timeline_lok);

        auto rit = _timeline_results.find(stream_name);
        if(rit != end(_timeline_results) && rit->second.ready)
        {
            cbs.set_contents(rit->second.segments);
            if(!rit->second.first_ts.is_null())
                cbs.scrollback_limit.set_value(rit->second.first_ts.value());
            cbs.set_motion_events(rit->second.motion_events);
            cbs.set_analytics_events(rit->second.analytics_events);
            rit->second.ready = false;
        }

        // Coalesced request: only the latest range per stream is kept, so a
        // slow fetch can't cause a backlog.
        _timeline_pending[stream_name] = timeline_request{camera_id, range.first, range.second};
    }
    _timeline_cv.notify_one();
}

void pipeline_host::_timeline_entry_point()
{
    for(;;)
    {
        std::string stream_name;
        timeline_request req;
        {
            unique_lock<mutex> g(_timeline_lok);
            _timeline_cv.wait(g, [&]{ return !_timeline_running || !_timeline_pending.empty(); });
            if(!_timeline_running)
                return;

            // Take one pending request (any stream).
            auto it = begin(_timeline_pending);
            stream_name = it->first;
            req = it->second;
            _timeline_pending.erase(it);
        }

        // The blocking part — off the UI thread. A failure (revere down) just
        // leaves the last-known result in place; the timeline keeps showing
        // whatever it last had instead of freezing.
        try
        {
            auto cr = query_segments(_cfg, req.camera_id, req.start, req.end);
            auto motion_events = query_motion_events(_cfg, req.camera_id, req.start, req.end);
            auto analytics_events = query_analytics(_cfg, req.camera_id, req.start, req.end);

            lock_guard<mutex> g(_timeline_lok);
            auto& res = _timeline_results[stream_name];
            res.segments = std::move(cr.segments);
            res.first_ts = cr.first_ts;
            res.motion_events = std::move(motion_events);
            res.analytics_events = std::move(analytics_events);
            res.ready = true;
        }
        catch(const std::exception& e)
        {
            R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
        }
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
        sok.connect("127.0.0.1", 8088);

        r_http::r_client_request req("127.0.0.1", 8088);
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
            auto body = res.get_body_as_string();
            if(!body.is_null())
                cbs.export_id = json::parse(body.value())["id"].get<string>();
            cbs.export_percent_complete = 0;
            cbs.exp_state = EXPORT_STATE_IN_PROGRESS;
            R_LOG_INFO("Export enqueued, id=%s", cbs.export_id.c_str());
        }
        else
        {
            cbs.exp_state = EXPORT_STATE_FINISHED_ERROR;
            R_LOG_INFO("Export request failed.");
        }
    }
}

void pipeline_host::control_bar_export_progress_cb(const std::string& /*stream_name*/, control_bar_state& cbs)
{
    int percent = query_export_progress(_cfg, cbs.export_id);
    if(percent < 0)
        return;  // transient error, retry next poll

    cbs.export_percent_complete = percent;

    if(percent >= 100)
        cbs.exp_state = EXPORT_STATE_FINISHED_SUCCESS;
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

std::chrono::system_clock::time_point pipeline_host::last_control_bar_pos(const std::string& stream_name) const
{
    lock_guard<mutex> pipes_lock(_internals_lok);
    auto found_pipe = _pipes.find(stream_name);
    if(found_pipe != end(_pipes))
        return found_pipe->second->get_last_control_bar_pos();
    return std::chrono::system_clock::time_point{};
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

            // DEADLOCK/STALL FIX: same pattern as disconnect_stream(). Collect
            // dead pipes under the lock but stop and destroy them only after
            // releasing it. pipeline_state teardown blocks (gst state change up
            // to 5s, then a join of the decode thread, which itself needs
            // _internals_lok inside post_video_frame) — doing that while
            // holding the lock froze the UI thread for the duration, and could
            // deadlock outright if the decode thread was mid-post. With revere
            // gone every pane dies at once, so this path used to stall the app
            // hard enough that it had to be killed.
            std::vector<std::shared_ptr<pipeline_state>> dead_pipes;
            {
                lock_guard<mutex> pipes_lock(_internals_lok);

                // If collect_stream_info() returned nothing at change_layout() time
                // (local server not ready), retry now so the layout eventually connects.
                if(_stream_infos.empty() && _retry_window != -1)
                {
                    auto sis = _cfg.collect_stream_info(_retry_window, _retry_layout);
                    if(!sis.empty())
                    {
                        R_LOG_INFO("pipeline_host: stream info now available, populating %zu streams", sis.size());
                        for(auto& si : sis)
                            _stream_infos.insert(make_pair(si.name, si));
                    }
                }
                auto curr = begin(_pipes);
                while(curr != end(_pipes))
                {
                    bool found_dead = false;

                    // Skip dead check for recently-started pipelines (playback needs time to start)
                    if(curr->second->ready_for_dead_check())
                    {
                        if(curr->second->running() && curr->second->last_v_pts() == curr->second->v_pts_at_check())
                            found_dead = true;

                        if(curr->second->running() && curr->second->has_audio() && curr->second->last_a_pts() == curr->second->a_pts_at_check())
                            found_dead = true;
                    }

                    if(found_dead)
                    {
                        R_LOG_ERROR("Dead stream detected");
                        dead_pipes.push_back(curr->second);
                        curr = _pipes.erase(curr);
                    }
                    else
                    {
                        curr->second->set_v_pts_at_check(curr->second->last_v_pts());
                        curr->second->set_a_pts_at_check(curr->second->last_a_pts());
                        ++curr;
                    }
                }
            }  // _internals_lok released here

            // Stop and destroy outside the critical section, on this worker
            // thread — the UI keeps rendering while teardown blocks.
            for(auto& p : dead_pipes)
                p->stop();
            dead_pipes.clear();
        }
    }
}
