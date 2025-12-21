#ifndef __r_motion_utils_h
#define __r_motion_utils_h

#include "r_utils/r_macro.h"
#include <vector>
#include <cstdint>

namespace r_motion
{

enum r_motion_image_type
{
    R_MOTION_IMAGE_TYPE_ARGB,
    R_MOTION_IMAGE_TYPE_BGR,
    R_MOTION_IMAGE_TYPE_RGB,
    R_MOTION_IMAGE_TYPE_GRAY8
};

struct r_image
{
    r_motion_image_type type;
    uint16_t width;
    uint16_t height;
    std::vector<uint8_t> data;
};

/**
 * Determines if motion is statistically significant using standard deviation analysis
 * @param motion Current motion value
 * @param avg_motion Average motion over time
 * @param stddev Standard deviation of motion
 * @param multiplier Standard deviation multiplier (default 2.0)
 * @return True if motion is significant (motion > avg + stddev * multiplier)
 */
R_API bool is_motion_significant(uint64_t motion, uint64_t avg_motion, uint64_t stddev, double multiplier = 2.0);

}

#endif