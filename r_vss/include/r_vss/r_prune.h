
#ifndef __r_vss_r_prune_h
#define __r_vss_r_prune_h

#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include "r_vss/r_ws.h"
#include "r_disco/r_camera.h"
#include <thread>
#include <deque>
#include <chrono>

namespace r_vss
{

struct prune_state
{
    r_disco::r_camera camera;
    std::vector<segment> blocks;
    size_t bi;
};

class r_prune final
{
public:
    R_API r_prune(r_ws& ws);
    R_API ~r_prune() noexcept;

    R_API void start();
    R_API void stop();

private:
    void _entry_point();
    void _update_cameras();
    void _rotate_cameras();

    bool _running;
    std::thread _prune_th;
    r_ws& _ws;

    std::deque<r_disco::r_camera> _cameras;
    std::chrono::system_clock::time_point _last_camera_fetch;

    r_utils::r_nullable<prune_state> _ps;
};

}

#endif
