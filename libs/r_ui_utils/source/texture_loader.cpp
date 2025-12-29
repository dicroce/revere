
#include "r_ui_utils/texture_loader.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_std_utils.h"
#include "r_ui_utils/stb_image.h"

using namespace std;
using namespace r_utils;
using namespace r_utils::r_std_utils;
using namespace r_ui_utils;

GLuint texture_loader::create_texture()
{
    texture_load_request r;
    r.type = load_type_create_texture;

    auto result = _load_q.post(r).get();
    if(!result.success)
        R_THROW(("texture_loader::create_texture: failed to create texture"));
    return result.texture_id;
}

void texture_loader::destroy_texture(GLuint texture_id)
{
    texture_load_request r;
    r.type = load_type_destroy_texture;
    r.texture_id = texture_id;

    auto result = _load_q.post(r).get();
    if(!result.success)
        R_THROW(("texture_loader::destroy_texture: failed to destroy texture"));
}

pair<uint16_t, uint16_t> texture_loader::load_texture_from_image_memory(GLuint texture_id, const uint8_t* data, size_t size)
{
    texture_load_request r;
    r.type = load_type_image_memory;
    r.texture_id = texture_id;
    r.data = data;
    r.size = size;

    auto result = _load_q.post(r).get();
    if(!result.success)
        R_THROW(("texture_loader::load_texture_from_rgb_buffer: failed to load texture"));
    return make_pair(result.width, result.height);
}

pair<uint16_t, uint16_t> texture_loader::load_texture_from_image_file(GLuint texture_id, const std::string& filename)
{
    texture_load_request r;
    r.type = load_type_image_file;
    r.texture_id = texture_id;
    r.filename = filename;

    auto result = _load_q.post(r).get();
    if(!result.success)
        R_THROW(("texture_loader::load_texture_from_image_file: failed to load texture"));
    return make_pair(result.width, result.height);
}

void texture_loader::load_texture_from_rgb_memory(GLuint texture_id, const uint8_t* data, size_t size, uint16_t width, uint16_t height)
{
    texture_load_request r;
    r.type = load_type_rgb_memory;
    r.texture_id = texture_id;
    r.data = data;
    r.size = size;
    r.width = width;
    r.height = height;

    auto result = _load_q.post(r).get();
    if(!result.success)
        R_THROW(("texture_loader::load_texture_from_rgb_buffer: failed to load texture"));
}

void texture_loader::work()
{
    bool done = false;

    while(!done)
    {
        auto w = _load_q.try_poll();

        if(w.is_null())
        {
            done = true;
            continue;
        }
        else
        {
            switch(w.raw().first.type)
            {
                case load_type_create_texture:
                {
                    GLuint new_texture_id;
                    glGenTextures(1, &new_texture_id);

                    texture_load_response response;
                    response.success = true;
                    response.texture_id = new_texture_id;
                    w.raw().second.set_value(response);
                }
                break;

                case load_type_destroy_texture:
                {
                    glDeleteTextures(1, &w.raw().first.texture_id);

                    texture_load_response response;
                    response.success = true;
                    w.raw().second.set_value(response);
                }
                break;

                case load_type_image_memory:
                {
                    int channels = 0;
                    int out_width = 0, out_height = 0;
                    raii_ptr<unsigned char> image_data(stbi_load_from_memory(w.raw().first.data, (int)w.raw().first.size, &out_width, &out_height, &channels, 3), stbi_image_free);
                    if(image_data.get() == nullptr)
                        R_THROW(("Unable to load image!"));

                    glBindTexture(GL_TEXTURE_2D, w.raw().first.texture_id);

                    // Setup filtering parameters for display
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#if 0
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same
#endif
                    // Upload pixels into texture
                #if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                #endif
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, out_width, out_height, 0, GL_RGB, GL_UNSIGNED_BYTE, image_data.get());

                    texture_load_response response;
                    response.success = true;
                    response.width = (uint16_t)out_width;
                    response.height = (uint16_t)out_height;
                    w.raw().second.set_value(response);
                }
                break;
                case load_type_image_file:
                {
                    int channels = 0;
                    int out_width = 0, out_height = 0;
                    raii_ptr<unsigned char> image_data(stbi_load(w.raw().first.filename.c_str(), &out_width, &out_height, &channels, 3), stbi_image_free);
                    if(image_data.get() == nullptr)
                        R_THROW(("Unable to load image!"));

                    glBindTexture(GL_TEXTURE_2D, w.raw().first.texture_id);

                    // Setup filtering parameters for display
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#if 0
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same
#endif
                    // Upload pixels into texture
                #if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                #endif
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, out_width, out_height, 0, GL_RGB, GL_UNSIGNED_BYTE, image_data.get());

                    texture_load_response response;
                    response.success = true;
                    response.width = (uint16_t)out_width;
                    response.height = (uint16_t)out_height;
                    w.raw().second.set_value(response);
                }
                break;
                case load_type_rgb_memory:
                {
                    glBindTexture(GL_TEXTURE_2D, w.raw().first.texture_id);

                    // Setup filtering parameters for display
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#if 0
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same
#endif
                    // Upload pixels into texture
                #if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                #endif
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w.raw().first.width, w.raw().first.height, 0, GL_RGB, GL_UNSIGNED_BYTE, w.raw().first.data);

                    texture_load_response response;
                    response.success = true;
                    response.width = w.raw().first.width;
                    response.height = w.raw().first.height;
                    w.raw().second.set_value(response);
                }
                break;
                default:
                    R_THROW(("texture_loader::work: unknown load type"));
            }
        }
    }
}