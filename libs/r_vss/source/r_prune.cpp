
#include "r_vss/r_prune.h"
#include "r_vss/r_ws.h"
#include "r_vss/r_query.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_time_utils.h"

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

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

    _prune_th.join();
}

void r_prune::_entry_point()
{
    while(_running)
    {
        try
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            auto now = system_clock::now();

            // - Periodically, fetch the camera list
            if(now - _last_camera_fetch > seconds(30))
            {
                _last_camera_fetch = now;
                _update_cameras();
            }

            if(!_cameras.empty())
            {
                if(_ps.is_null())
                {
                    prune_state ps;
                    ps.camera = _cameras.front();
                    auto blocks = query_get_blocks(_top_dir, _devices, ps.camera.id);

                    if(blocks.empty())
                        _rotate_cameras();
                    else
                    {
                        ps.blocks = blocks;
                        ps.bi = 0;
                        _ps = ps;
                    }
                }
                else
                {
                    auto current_ps = _ps.value();

                    auto block_start = current_ps.blocks[current_ps.bi].start;
                    auto block_end = current_ps.blocks[current_ps.bi].end;

                    // Skip blocks that are too recent - the +30 second overlap on the motion
                    // query would exceed what the ring buffer has recorded
                    if(block_end + chrono::seconds(30) > now)
                    {
                        _rotate_cameras();
                        _ps.clear();
                        continue;
                    }

                    auto retention_cutoff = now - chrono::hours(current_ps.camera.min_continuous_recording_hours.value());
                    if(block_start > retention_cutoff)
                    {
                        _rotate_cameras();
                        _ps.clear();
                        continue;
                    }

                    auto motion_events = query_get_motion_events(
                        _top_dir,
                        _devices,
                        current_ps.camera.id,
                        block_start - chrono::seconds(30),
                        block_end + chrono::seconds(30)
                    );

                    if(motion_events.empty())
                    {
                        R_LOG_INFO("r_prune: pruning %s FROM %s -> %s\n",
                            current_ps.camera.friendly_name.value().c_str(),
                            r_time_utils::tp_to_iso_8601(block_start, false).c_str(),
                            r_time_utils::tp_to_iso_8601(block_end, false).c_str()
                        );
                        query_remove_blocks(
                            _top_dir,
                            _devices,
                            current_ps.camera.id,
                            block_start,
                            block_end
                        );
                    }

                    ++current_ps.bi;

                    if(current_ps.bi >= current_ps.blocks.size())
                    {
                        _rotate_cameras();
                        _ps.clear();
                    }
                    else _ps = current_ps;
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
