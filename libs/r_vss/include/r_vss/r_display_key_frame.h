#ifndef __r_vss_r_display_key_frame_h
#define __r_vss_r_display_key_frame_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <memory>
#include <vector>

// Kept apart from r_query.h, which brings in nanots, so a UI can include it.

namespace r_vss
{

// A GET /key_frame reply (query_get_key_frame's blob tree), decoded for
// display: scaled to fit inside w x h with its aspect kept (never enlarged),
// as BGRA, or as NV12 when nv12 is set. pixels is null when the reply holds no
// frame (nothing recorded in the last 5 minutes) or it doesn't decode.
struct r_display_key_frame
{
    int64_t ts {0};             // the key frame's time, ms since the epoch
    std::shared_ptr<std::vector<uint8_t>> pixels;
    bool nv12 {false};
    uint16_t w {0};
    uint16_t h {0};
    uint16_t source_w {0};      // the recording's resolution
    uint16_t source_h {0};
};

R_API r_display_key_frame decode_key_frame_reply(const std::vector<uint8_t>& reply, bool nv12, uint16_t w, uint16_t h);

}

#endif
