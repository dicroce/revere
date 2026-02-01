
#include "gl_utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_std_utils.h"
#include "r_ui_utils/stb_image.h"

using namespace std;
using namespace r_utils;
using namespace r_utils::r_std_utils;

std::shared_ptr<r_ui_utils::texture> revere::load_texture_from_image_file(SDL_Renderer* renderer, const string& filename)
{
    int w = 0, h = 0, channels = 0;
    raii_ptr<unsigned char> image_data(stbi_load(filename.c_str(), &w, &h, &channels, 4), stbi_image_free);
    if(image_data.get() == nullptr)
        R_THROW(("Unable to load image!"));

    // stb_image returns RGBA, but SDL ARGB8888 on little-endian expects BGRA
    // Swap R and B channels in-place
    unsigned char* pixels = image_data.get();
    for (int i = 0; i < w * h * 4; i += 4)
    {
        unsigned char temp = pixels[i];     // Save R
        pixels[i] = pixels[i + 2];          // B -> R position
        pixels[i + 2] = temp;               // R -> B position
    }

    return r_ui_utils::texture::create_from_rgba(renderer, image_data.get(), (uint16_t)w, (uint16_t)h);
}

std::shared_ptr<r_ui_utils::texture> revere::load_texture_from_image_memory(SDL_Renderer* renderer, const uint8_t* data, size_t size)
{
    int w = 0, h = 0, channels = 0;
    raii_ptr<unsigned char> image_data(stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4), stbi_image_free);
    if(image_data.get() == nullptr)
        R_THROW(("Unable to load image!"));

    // stb_image returns RGBA, but SDL ARGB8888 on little-endian expects BGRA
    // Swap R and B channels in-place
    unsigned char* pixels = image_data.get();
    for (int i = 0; i < w * h * 4; i += 4)
    {
        unsigned char temp = pixels[i];     // Save R
        pixels[i] = pixels[i + 2];          // B -> R position
        pixels[i + 2] = temp;               // R -> B position
    }

    return r_ui_utils::texture::create_from_rgba(renderer, image_data.get(), (uint16_t)w, (uint16_t)h);
}

std::shared_ptr<r_ui_utils::texture> revere::load_texture_from_rgba(SDL_Renderer* renderer, const uint8_t* pixels, uint16_t w, uint16_t h)
{
    return r_ui_utils::texture::create_from_rgba(renderer, pixels, w, h);
}
