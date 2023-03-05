
#include "r_vss/r_prune.h"
#include "r_vss/r_ws.h"
#include "r_utils/r_exception.h"

using namespace r_vss;
using namespace r_utils;
using namespace std;

r_prune::r_prune(r_ws& ws) :
    _running(false),
    _prune_th(),
    _ws(ws)
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
            // - Periodically, fetch the camera list
            // - Have a cache of cameras we're pruning
            // - For each camera, start at the time of the oldest video and work forward till now
            // -   For each 5 minute block from oldest video till now
            // -     If block is not in last 24 hours
            // -         Query motions for this block
            // -         If there are no motions, issue delete for the block
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        catch(const std::exception& e)
        {
            R_LOG_ERROR("Pruning Exception: %s", e.what());
        }
    }
}
