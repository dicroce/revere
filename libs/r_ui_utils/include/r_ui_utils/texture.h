
#ifndef __r_ui_utils_texture_h
#define __r_ui_utils_texture_h

#include "r_utils/r_macro.h"
#include <SDL.h>
#include <memory>
#include <cstdint>

namespace r_ui_utils
{

// Forward declaration for ImGui compatibility
struct ImTextureID;

class texture final
{
public:
    R_API texture();
    R_API ~texture();

    // Non-copyable
    texture(const texture&) = delete;
    texture& operator=(const texture&) = delete;

    // Movable
    R_API texture(texture&& other) noexcept;
    R_API texture& operator=(texture&& other) noexcept;

    // Create texture from RGBA pixel data
    R_API static std::shared_ptr<texture> create_from_rgba(
        SDL_Renderer* renderer,
        const uint8_t* pixels,
        uint16_t w,
        uint16_t h);

    // Create texture from RGB pixel data
    R_API static std::shared_ptr<texture> create_from_rgb(
        SDL_Renderer* renderer,
        const uint8_t* pixels,
        uint16_t w,
        uint16_t h);

    // Create empty texture with streaming access (for video frames)
    R_API static std::shared_ptr<texture> create_streaming(
        SDL_Renderer* renderer,
        uint16_t w,
        uint16_t h,
        bool rgba = true);

    // Update existing texture with new RGBA pixel data
    R_API bool update_rgba(const uint8_t* pixels, uint16_t w, uint16_t h);

    // Update existing texture with new RGB pixel data
    R_API bool update_rgb(const uint8_t* pixels, uint16_t w, uint16_t h);

    // Get dimensions
    R_API uint16_t width() const { return _width; }
    R_API uint16_t height() const { return _height; }

    // Check if texture is valid
    R_API bool is_valid() const { return _sdl_texture != nullptr; }

    // Get ImTextureID for ImGui::Image()
    // ImGui's SDL2Renderer backend uses SDL_Texture* directly as ImTextureID
    R_API void* imgui_id() const { return (void*)_sdl_texture; }

    // Get raw SDL texture (for advanced usage)
    R_API SDL_Texture* sdl_texture() const { return _sdl_texture; }

private:
    SDL_Texture* _sdl_texture = nullptr;
    SDL_Renderer* _renderer = nullptr;
    uint16_t _width = 0;
    uint16_t _height = 0;
    bool _is_rgba = true;
};

} // namespace r_ui_utils

#endif
