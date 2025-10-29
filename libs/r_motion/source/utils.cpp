#include "r_motion/utils.h"

using namespace r_motion;

bool r_motion::is_motion_significant(uint64_t motion, uint64_t avg_motion, uint64_t stddev, double multiplier)
{
    return motion > avg_motion + (stddev * multiplier);
}