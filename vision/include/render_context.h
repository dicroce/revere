
#ifndef __vision_render_context_h
#define __vision_render_context_h

#include <GLFW/glfw3.h> // Will drag system OpenGL headers
#include <cstdint>

namespace vision
{

struct render_context final
{
    render_context() :
        texture_id(0),
        w(0),
        h(0),
        done(false)
    {
    }
    ~render_context()
    {
    }
    GLuint texture_id;
    uint16_t w;
    uint16_t h;
    bool done;
};

}

#endif
