
#include "r_vss/r_prune.h"
#include "r_vss/r_ws.h"
#include "r_vss/r_query.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_time_utils.h"

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

static const chrono::minutes PRUNE_CHUNK_SIZE{15};

r_prune::r_prune(const std::string& top_dir, r_disco::r_devices& devices) :
    _running(false),
    _prune_th(),
    _top_dir(top_dir),
    _devices(devices),
    _cameras(),
    _last_camera_fetch(system_clock::time_point{}),
    _ps()
{
}

r_prune::~r_prune() noexcept
{
    if(_running)
        stop();
}

void r_prune::start()
{
    if(_running)
        R_THROW(("r_prune already started!"));

    _running = true;

    _prune_th = thread(&r_prune::_entry_point, this);
}

void r_prune::stop()
{
    if(!_running)
        R_THROW(("Cannot stop r_prune if its not running!"));

    _running = false;
    _stop_cv.notify_all();

    _prune_th.join();
}

void r_prune::_entry_point()
{
    while(_running)
    {
        {
            std::unique_lock<std::mutex> lock(_stop_mutex);
            _stop_cv.wait_for(lock, std::chrono::seconds(1), [this]{ return !_running.load(); });
        }

        if(!_running)
            break;

        try
        {
            auto now = system_clock::now();

            // - Periodically, fetch the camera list
            if(now - _last_camera_fetch > seconds(30))
            {
                _last_camera_fetch = now;
                _update_cameras();
            }

            if(!_running) break;

            if(!_cameras.empty())
            {
                if(_ps.is_null())
                {
                    prune_state ps;
                    ps.camera = _cameras.front();

                    auto first_ts = query_get_first_ts(_top_dir, _devices, ps.camera.id);
                    auto retention_cutoff = now - chrono::hours(ps.camera.min_continuous_recording_hours.value());

                    if(first_ts.is_null() || first_ts.value() >= retention_cutoff)
                    {
                        _rotate_cameras();
                    }
                    else
                    {
                        auto it = _prune_hwm.find(ps.camera.id);
                        if(it != _prune_hwm.end())
                            ps.cursor = max(first_ts.value(), it->second - PRUNE_CHUNK_SIZE);
                        else
                            ps.cursor = first_ts.value();

                        R_LOG_INFO("r_prune: switching to camera %s\n", ps.camera.friendly_name.value().c_str());
                        _ps = ps;
                    }
                }
                else
                {
                    if(!_running) break;

                    auto current_ps = _ps.value();

                    auto retention_cutoff = now - chrono::hours(current_ps.camera.min_continuous_recording_hours.value());

                    if(current_ps.cursor >= retention_cutoff)
                    {
                        _prune_hwm[current_ps.camera.id] = retention_cutoff;
                        _rotate_cameras();
                        _ps.clear();
                        continue;
                    }

                    auto chunk_end = current_ps.cursor + PRUNE_CHUNK_SIZE;
                    if(chunk_end > retention_cutoff)
                        chunk_end = retention_cutoff;

                    // Not enough remaining range to meaningfully prune - done with this camera
                    if(chunk_end - current_ps.cursor < PRUNE_CHUNK_SIZE / 2)
                    {
                        _prune_hwm[current_ps.camera.id] = retention_cutoff;
                        _rotate_cameras();
                        _ps.clear();
                        continue;
                    }

                    // Don't process chunks whose end would require querying beyond now
                    if(chunk_end + chrono::seconds(30) > now)
                    {
                        _prune_hwm[current_ps.camera.id] = retention_cutoff;
                        _rotate_cameras();
                        _ps.clear();
                        continue;
                    }

                    auto motion_events = query_get_motion_events(
                        _top_dir,
                        _devices,
                        current_ps.camera.id,
                        current_ps.cursor - chrono::seconds(30),
                        chunk_end + chrono::seconds(30)
                    );

                    if(!_running) break;

                    if(motion_events.empty())
                    {
                        query_remove_blocks(
                            _top_dir,
                            _devices,
                            current_ps.camera.id,
                            current_ps.cursor,
                            chunk_end
                        );
                    }

                    current_ps.cursor = chunk_end;
                    _ps = current_ps;
                }
            }

        }
        catch(const std::exception& e)
        {
            R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
            printf("Pruning Exception: %s\n", e.what());
        }
    }
}

void r_prune::_update_cameras()
{
    auto cameras = query_get_cameras(_devices);

    // Remove cameras that are no longer eligible (pruning disabled or unassigned)
    for(auto it = _cameras.begin(); it != _cameras.end(); )
    {
        bool still_eligible = false;
        for(auto& c : cameras)
        {
            if(c.id == it->id && c.do_motion_pruning.value() == true && c.state == "assigned")
            {
                *it = c; // refresh in case fields changed
                still_eligible = true;
                break;
            }
        }

        if(!still_eligible)
        {
            if(!_ps.is_null() && _ps.value().camera.id == it->id)
                _ps.clear();
            _prune_hwm.erase(it->id);
            it = _cameras.erase(it);
        }
        else ++it;
    }

    // Add newly eligible cameras not yet in the list
    for(auto& c : cameras)
    {
        if(c.do_motion_pruning.value() != true || c.state != "assigned")
            continue;

        bool found = false;
        for(auto& cc : _cameras)
        {
            if(cc.id == c.id) { found = true; break; }
        }

        if(!found)
            _cameras.push_back(c);
    }
}

void r_prune::_rotate_cameras()
{
    if(_cameras.empty())
        return;

    auto c = _cameras.front();
    _cameras.pop_front();
    _cameras.push_back(c);
}
