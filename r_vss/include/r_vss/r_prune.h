
#ifndef __r_vss_r_prune_h
#define __r_vss_r_prune_h

#include "r_utils/r_macro.h"
#include "r_vss/r_ws.h"
#include <thread>

namespace r_vss
{

class r_prune final
{
public:
    R_API r_prune(r_ws& ws);
    R_API ~r_prune() noexcept;

    R_API void start();
    R_API void stop();

private:
    void _entry_point();

    bool _running;
    std::thread _prune_th;
    r_ws& _ws;
};

}

#endif
