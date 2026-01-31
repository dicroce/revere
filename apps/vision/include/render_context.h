
#ifndef __vision_render_context_h
#define __vision_render_context_h

#include <SDL.h>
#include <cstdint>
#include <memory>
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
        pts(0)
    {
    }
    ~render_context() = default;

    std::shared_ptr<r_ui_utils::texture> tex;
    uint16_t w;
    uint16_t h;
    bool done;
    int64_t pts;  // Presentation timestamp of current frame
};

}

#endif
