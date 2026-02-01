
#ifndef __revere_gl_utils_h
#define __revere_gl_utils_h

#include <SDL.h>
#include <string>
#include <memory>
#include "r_ui_utils/texture.h"

namespace revere
{

std::shared_ptr<r_ui_utils::texture> load_texture_from_image_file(SDL_Renderer* renderer, const std::string& filename);

std::shared_ptr<r_ui_utils::texture> load_texture_from_image_memory(SDL_Renderer* renderer, const uint8_t* data, size_t size);

std::shared_ptr<r_ui_utils::texture> load_texture_from_rgba(SDL_Renderer* renderer, const uint8_t* pixels, uint16_t w, uint16_t h);

}

#endif
