#ifndef __r_vss_r_vss_utils_h
#define __r_vss_r_vss_utils_h

#include <vector>
#include <cstdint>
#include "r_utils/r_macro.h"

namespace r_vss
{

R_API std::vector<std::pair<int64_t, int64_t>> find_contiguous_segments(const std::vector<int64_t>& times);

}

#endif
