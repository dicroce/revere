
#ifndef __vision_render_context_h
#define __vision_render_context_h

#include <SDL.h>
#include <cstdint>
#include <memory>
#include <vector>
#include "r_ui_utils/texture.h"

namespace vision
{

struct render_context final
{
    render_context() :
        tex(nullptr),
        w(0),
        h(0),
        done(false),
        nv12(false),
        pts(0)
    {
    }
    ~render_context() = default;

    std::shared_ptr<r_ui_utils::texture> tex;
    uint16_t w;
    uint16_t h;
    bool done;
    bool nv12;    // tex is an NV12 texture
    int64_t pts;  // Presentation timestamp of current frame

    // Pixels of the frame currently on the texture, kept for snapshots. Only
    // the UI thread (load_video_textures) writes these, and only the UI thread
    // reads them, so no lock is needed. The dims are the frame's own: rc->w/h
    // may already hold a pending resize the texture hasn't caught up with.
    std::shared_ptr<std::vector<uint8_t>> last_frame;
    bool last_frame_nv12 {false};
    uint16_t last_frame_w {0};
    uint16_t last_frame_h {0};
    uint16_t last_frame_original_h {0};  // camera height, picks the YUV colour matrix
};

}

#endif
