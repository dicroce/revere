
#ifndef __r_ui_utils_texture_loader_h
#define __r_ui_utils_texture_loader_h

#include "r_utils/r_work_q.h"
#include "r_utils/r_macro.h"
#include "r_ui_utils/texture.h"
#include <SDL.h>
#include <map>
#include <cstdint>
#include <memory>

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
    std::shared_ptr<texture> tex;
    std::string filename;
    const uint8_t* data;
    size_t size;
    uint16_t width;
    uint16_t height;
    SDL_Renderer* renderer;
};

struct texture_load_response
{
    bool success;
    uint16_t width;
    uint16_t height;
    std::shared_ptr<texture> tex;
};

class texture_loader final
{
public:
    R_API texture_loader() = default;

    // Set the renderer to use for creating textures
    R_API void set_renderer(SDL_Renderer* renderer) { _renderer = renderer; }

    R_API std::shared_ptr<texture> create_texture();
    R_API void destroy_texture(std::shared_ptr<texture> tex);
    R_API std::pair<uint16_t, uint16_t> load_texture_from_image_memory(std::shared_ptr<texture> tex, const uint8_t* data, size_t size);
    R_API std::pair<uint16_t, uint16_t> load_texture_from_image_file(std::shared_ptr<texture> tex, const std::string& filename);
    R_API void load_texture_from_rgb_memory(std::shared_ptr<texture> tex, const uint8_t* data, size_t size, uint16_t width, uint16_t height);

    R_API void work();

private:
    SDL_Renderer* _renderer = nullptr;
    r_utils::r_work_q<texture_load_request,texture_load_response> _load_q;
};

}

#endif
