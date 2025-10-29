
#ifndef __r_ui_utils_texture_loader_h
#define __r_ui_utils_texture_loader_h

#include "r_utils/r_work_q.h"
#include "r_utils/r_macro.h"
#include <GLFW/glfw3.h> // Will drag system OpenGL headers
#include <map>
#include <cstdint>

namespace r_ui_utils
{

enum load_type
{
    load_type_create_texture,
    load_type_destroy_texture,
    load_type_image_file,
    load_type_image_memory,
    load_type_rgb_memory
};

struct texture_load_request
{
    load_type type;
    GLuint texture_id;
    std::string filename;
    const uint8_t* data;
    size_t size;
    uint16_t width;
    uint16_t height;
};

struct texture_load_response
{
    bool success;
    uint16_t width;
    uint16_t height;
    GLuint texture_id;
};

class texture_loader final
{
public:
    R_API GLuint create_texture();
    R_API void destroy_texture(GLuint texture_id);
    R_API std::pair<uint16_t, uint16_t> load_texture_from_image_memory(GLuint texture_id, const uint8_t* data, size_t size);
    R_API std::pair<uint16_t, uint16_t> load_texture_from_image_file(GLuint texture_id, const std::string& filename);
    R_API void load_texture_from_rgb_memory(GLuint texture_id, const uint8_t* data, size_t size, uint16_t width, uint16_t height);

    R_API void work();

private:
    r_utils::r_work_q<texture_load_request,texture_load_response> _load_q;
};

}

#endif
