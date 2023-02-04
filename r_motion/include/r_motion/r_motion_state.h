
#ifndef __r_motion_r_motion_state_h__
#define __r_motion_r_motion_state_h__

#include "r_motion/utils.h"
#include "r_utils/r_avg.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <functional>

namespace r_motion
{

struct r_motion_info
{
    r_image motion_pixels;
    uint64_t motion {0};
    uint64_t avg_motion {0};
    uint64_t stddev {0};
};

class r_motion_state
{
public:
    R_API r_motion_state(size_t memory=500);
    R_API r_motion_state(const r_motion_state&) = delete;
    R_API r_motion_state(r_motion_state&& obj);
    R_API ~r_motion_state() noexcept;

    R_API r_motion_state& operator=(const r_motion_state&) = delete;
    R_API r_motion_state& operator=(r_motion_state&& obj);

    R_API r_utils::r_nullable<r_motion_info> process(const r_image& argb_input);

private:
    r_utils::r_exp_avg<uint64_t> _avg_motion;
    bool _has_last;
    r_image _last;
    bool _has_last_motion;
    r_image _last_motion;
};

}

#endif
