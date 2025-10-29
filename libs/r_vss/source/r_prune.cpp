
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
    _last_camera_fetch(system_clock::now()),
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
            // Current pruning has a bug:
            // - Consider a motion event that spans 3 blocks, but whose motion
            //   threshold during the middle block is not high enough to create
            //   an event.  The middle block will be pruned.
            
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

                    auto motion_events = query_get_motion_events(
                        _top_dir,
                        _devices,
                        current_ps.camera.id,
                        1,
                        block_start - chrono::seconds(30),
                        block_end + chrono::seconds(30)
                    );

                    if(motion_events.empty())
                    {
#if 0
                        R_LOG_INFO(
                            "Pruning %s FROM %s -> %s\n", 
                            current_ps.camera.friendly_name.value().c_str(),
                            r_time_utils::tp_to_iso_8601(block_start, false).c_str(),
                            r_time_utils::tp_to_iso_8601(block_end, false).c_str()
                        );
#endif
                        query_remove_blocks(
                            _top_dir,
                            _devices,
                            current_ps.camera.id,
                            block_start,
                            block_end
                        );                        
                    }

                    ++current_ps.bi;

                    if(current_ps.bi >= current_ps.blocks.size() ||
                       block_start > (now - chrono::hours(current_ps.camera.min_continuous_recording_hours.value())))
                    {
                        _rotate_cameras();
                        _ps.clear();
                    }
                    else _ps = current_ps;
                }
            }

            // - Have a cache of cameras we're pruning
            // - For each camera, start at the time of the oldest video and work forward till now
            // -   For each 5 minute block from oldest video till now
            // -     If block is not in last 24 hours
            // -         Query motions for this block
            // -         If there are no motions, issue delete for the block
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

    for(auto& c : cameras)
    {
        bool found = false;
        for(auto& cc : _cameras)
        {
            if(cc.id == c.id)
            {
                cc = c; // update cc in case a field has changed...
                found = true;
                break;
            }
        }

        if(!found && (c.do_motion_pruning.value() == true) && (c.state == "assigned"))
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
