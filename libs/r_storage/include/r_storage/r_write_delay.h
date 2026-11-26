#ifndef __r_storage_r_write_delay_h
#define __r_storage_r_write_delay_h

#include "r_utils/r_macro.h"
#include <cstdint>

namespace r_storage
{

// How long video frames wait to be written, for the pressure signal "recording writes behind" (r_vss/r_pressure.h).
// Every recording file's writer notes each video frame's wait (from the frame being handed to storage to its write
// finishing); the reader takes the longest since it last asked. One process-wide note: the question is whether
// writes are behind anywhere.

// Notes one write's wait, in ms (negative is taken as 0).
R_API void note_write_delay(int64_t ms);

// The longest wait noted since the last call, and clears it; -1 when none was noted (nothing was written).
R_API int64_t take_write_delay_max();

}

#endif
