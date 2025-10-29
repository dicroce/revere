
#ifndef r_utils_r_lower_bound_h
#define r_utils_r_lower_bound_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <functional>

namespace r_utils
{

// returns pointer to first element between start and end which does not compare less than target
template<typename CMP>
R_API uint8_t* lower_bound_bytes(uint8_t* start,
                                 uint8_t* end,
                                 uint8_t* target,
                                 size_t elementSize,
                                 CMP cmp)
{
    size_t N = (end - start) / elementSize;
    size_t low = 0, high = N;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        uint8_t* mid_elem = start + mid * elementSize;

        // If mid_elem is >= target, move left
        if (cmp(mid_elem, target) >= 0)
            high = mid;
        else
            low = mid + 1;
    }

    return start + low * elementSize;
}

}

#endif