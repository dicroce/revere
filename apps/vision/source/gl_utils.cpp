
#include "gl_utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace std;
using namespace r_utils;

std::shared_ptr<r_ui_utils::texture> vision::load_texture_from_file(SDL_Renderer* renderer, const string& filename)
{
    // Input validation
    if (!renderer)
    {
        R_LOG_ERROR("NULL renderer in load_texture_from_file");
        return nullptr;
    }

    if (filename.empty())
    {
        R_LOG_ERROR("Empty filename in load_texture_from_file");
        return nullptr;
    }

    // Load from file
    int image_width = 0;
    int image_height = 0;
    int channels = 0;
    unsigned char* image_data = stbi_load(filename.c_str(), &image_width, &image_height, &channels, 4);
    if (image_data == NULL)
    {
        R_LOG_ERROR("Failed to load image file: %s", filename.c_str());
        return nullptr;
    }

    auto tex = r_ui_utils::texture::create_from_rgba(
        renderer,
        image_data,
        (uint16_t)image_width,
        (uint16_t)image_height
    );

    stbi_image_free(image_data);

    return tex;
}

std::shared_ptr<r_ui_utils::texture> vision::load_texture_from_rgba(SDL_Renderer* renderer, const uint8_t* pixels, uint16_t w, uint16_t h)
{
    // Input validation
    if (!renderer || !pixels)
    {
        R_LOG_ERROR("NULL parameters in load_texture_from_rgba");
        return nullptr;
    }

    return r_ui_utils::texture::create_from_rgba(renderer, pixels, w, h);
}
