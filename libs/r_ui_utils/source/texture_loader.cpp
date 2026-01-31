
#include "r_ui_utils/texture_loader.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_std_utils.h"
#include "r_ui_utils/stb_image.h"

using namespace std;
using namespace r_utils;
using namespace r_utils::r_std_utils;
using namespace r_ui_utils;

shared_ptr<texture> texture_loader::create_texture()
{
    // Create a placeholder texture - actual creation happens in work()
    // For SDL2, we need the renderer to create textures, so we defer
    texture_load_request r;
    r.type = load_type_create_texture;
    r.renderer = _renderer;

    auto result = _load_q.post(r).get();
    if(!result.success)
        R_THROW(("texture_loader::create_texture: failed to create texture"));
    return result.tex;
}

void texture_loader::destroy_texture(shared_ptr<texture> tex)
{
    texture_load_request r;
    r.type = load_type_destroy_texture;
    r.tex = tex;

    auto result = _load_q.post(r).get();
    if(!result.success)
        R_THROW(("texture_loader::destroy_texture: failed to destroy texture"));
}

pair<uint16_t, uint16_t> texture_loader::load_texture_from_image_memory(shared_ptr<texture> tex, const uint8_t* data, size_t size)
{
    texture_load_request r;
    r.type = load_type_image_memory;
    r.tex = tex;
    r.data = data;
    r.size = size;
    r.renderer = _renderer;

    auto result = _load_q.post(r).get();
    if(!result.success)
        R_THROW(("texture_loader::load_texture_from_image_memory: failed to load texture"));
    return make_pair(result.width, result.height);
}

pair<uint16_t, uint16_t> texture_loader::load_texture_from_image_file(shared_ptr<texture> tex, const std::string& filename)
{
    texture_load_request r;
    r.type = load_type_image_file;
    r.tex = tex;
    r.filename = filename;
    r.renderer = _renderer;

    auto result = _load_q.post(r).get();
    if(!result.success)
        R_THROW(("texture_loader::load_texture_from_image_file: failed to load texture"));
    return make_pair(result.width, result.height);
}

void texture_loader::load_texture_from_rgb_memory(shared_ptr<texture> tex, const uint8_t* data, size_t size, uint16_t width, uint16_t height)
{
    texture_load_request r;
    r.type = load_type_rgb_memory;
    r.tex = tex;
    r.data = data;
    r.size = size;
    r.width = width;
    r.height = height;
    r.renderer = _renderer;

    auto result = _load_q.post(r).get();
    if(!result.success)
        R_THROW(("texture_loader::load_texture_from_rgb_memory: failed to load texture"));
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
                    // Create an empty placeholder texture
                    // We'll create the actual SDL texture when we have pixel data
                    auto new_tex = make_shared<texture>();

                    texture_load_response response;
                    response.success = true;
                    response.tex = new_tex;
                    w.raw().second.set_value(response);
                }
                break;

                case load_type_destroy_texture:
                {
                    // The texture will be destroyed when the shared_ptr goes out of scope
                    // Just mark as successful
                    texture_load_response response;
                    response.success = true;
                    w.raw().second.set_value(response);
                }
                break;

                case load_type_image_memory:
                {
                    int channels = 0;
                    int out_width = 0, out_height = 0;
                    raii_ptr<unsigned char> image_data(stbi_load_from_memory(w.raw().first.data, (int)w.raw().first.size, &out_width, &out_height, &channels, 4), stbi_image_free);
                    if(image_data.get() == nullptr)
                        R_THROW(("Unable to load image!"));

                    auto tex = texture::create_from_rgba(
                        w.raw().first.renderer,
                        image_data.get(),
                        (uint16_t)out_width,
                        (uint16_t)out_height
                    );

                    texture_load_response response;
                    response.success = (tex != nullptr);
                    response.width = (uint16_t)out_width;
                    response.height = (uint16_t)out_height;
                    response.tex = tex;
                    w.raw().second.set_value(response);
                }
                break;

                case load_type_image_file:
                {
                    int channels = 0;
                    int out_width = 0, out_height = 0;
                    raii_ptr<unsigned char> image_data(stbi_load(w.raw().first.filename.c_str(), &out_width, &out_height, &channels, 4), stbi_image_free);
                    if(image_data.get() == nullptr)
                        R_THROW(("Unable to load image!"));

                    auto tex = texture::create_from_rgba(
                        w.raw().first.renderer,
                        image_data.get(),
                        (uint16_t)out_width,
                        (uint16_t)out_height
                    );

                    texture_load_response response;
                    response.success = (tex != nullptr);
                    response.width = (uint16_t)out_width;
                    response.height = (uint16_t)out_height;
                    response.tex = tex;
                    w.raw().second.set_value(response);
                }
                break;

                case load_type_rgb_memory:
                {
                    auto tex = texture::create_from_rgb(
                        w.raw().first.renderer,
                        w.raw().first.data,
                        w.raw().first.width,
                        w.raw().first.height
                    );

                    texture_load_response response;
                    response.success = (tex != nullptr);
                    response.width = w.raw().first.width;
                    response.height = w.raw().first.height;
                    response.tex = tex;
                    w.raw().second.set_value(response);
                }
                break;

                default:
                    R_THROW(("texture_loader::work: unknown load type"));
            }
        }
    }
}
