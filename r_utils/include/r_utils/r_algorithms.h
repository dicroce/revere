
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
    if(cmp(target, start) <= 0)
        return start;

    const size_t N = (end - start) / elementSize;
    size_t mid;
    size_t low = 0;
    size_t high = N;

    while(low < high) {
        mid = low + (high - low) / 2;
        auto res = cmp(target, start + (mid * elementSize));
        if(res <= 0)
            high = mid;
        else
            low = mid + 1;
    }

    auto res = cmp(start + (low * elementSize), target);
    if(low < N && res < 0)
        low++;

    return start + (low * elementSize);
}

}

#endif