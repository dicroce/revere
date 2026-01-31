
#include "r_ui_utils/texture.h"
#include "r_utils/r_logger.h"

using namespace r_ui_utils;

texture::texture() :
    _sdl_texture(nullptr),
    _renderer(nullptr),
    _width(0),
    _height(0),
    _is_rgba(true)
{
}

texture::~texture()
{
    if (_sdl_texture)
    {
        SDL_DestroyTexture(_sdl_texture);
        _sdl_texture = nullptr;
    }
}

texture::texture(texture&& other) noexcept :
    _sdl_texture(other._sdl_texture),
    _renderer(other._renderer),
    _width(other._width),
    _height(other._height),
    _is_rgba(other._is_rgba)
{
    other._sdl_texture = nullptr;
    other._renderer = nullptr;
    other._width = 0;
    other._height = 0;
}

texture& texture::operator=(texture&& other) noexcept
{
    if (this != &other)
    {
        if (_sdl_texture)
            SDL_DestroyTexture(_sdl_texture);

        _sdl_texture = other._sdl_texture;
        _renderer = other._renderer;
        _width = other._width;
        _height = other._height;
        _is_rgba = other._is_rgba;

        other._sdl_texture = nullptr;
        other._renderer = nullptr;
        other._width = 0;
        other._height = 0;
    }
    return *this;
}

std::shared_ptr<texture> texture::create_from_rgba(
    SDL_Renderer* renderer,
    const uint8_t* pixels,
    uint16_t w,
    uint16_t h)
{
    if (!renderer || !pixels || w == 0 || h == 0)
    {
        R_LOG_ERROR("Invalid parameters for create_from_rgba");
        return nullptr;
    }

    auto tex = std::make_shared<texture>();
    tex->_renderer = renderer;
    tex->_width = w;
    tex->_height = h;
    tex->_is_rgba = true;

    // Create SDL texture with RGBA format
    tex->_sdl_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ABGR8888,  // RGBA format (ImGui expects this order)
        SDL_TEXTUREACCESS_STATIC,
        w,
        h
    );

    if (!tex->_sdl_texture)
    {
        R_LOG_ERROR("Failed to create RGBA texture: %s", SDL_GetError());
        return nullptr;
    }

    // Enable blending for alpha support
    SDL_SetTextureBlendMode(tex->_sdl_texture, SDL_BLENDMODE_BLEND);

    // Upload pixel data
    if (SDL_UpdateTexture(tex->_sdl_texture, nullptr, pixels, w * 4) != 0)
    {
        R_LOG_ERROR("Failed to update RGBA texture: %s", SDL_GetError());
        SDL_DestroyTexture(tex->_sdl_texture);
        tex->_sdl_texture = nullptr;
        return nullptr;
    }

    return tex;
}

std::shared_ptr<texture> texture::create_from_rgb(
    SDL_Renderer* renderer,
    const uint8_t* pixels,
    uint16_t w,
    uint16_t h)
{
    if (!renderer || !pixels || w == 0 || h == 0)
    {
        R_LOG_ERROR("Invalid parameters for create_from_rgb");
        return nullptr;
    }

    auto tex = std::make_shared<texture>();
    tex->_renderer = renderer;
    tex->_width = w;
    tex->_height = h;
    tex->_is_rgba = false;

    // Create SDL texture with RGB format
    tex->_sdl_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STATIC,
        w,
        h
    );

    if (!tex->_sdl_texture)
    {
        R_LOG_ERROR("Failed to create RGB texture: %s", SDL_GetError());
        return nullptr;
    }

    // Upload pixel data
    if (SDL_UpdateTexture(tex->_sdl_texture, nullptr, pixels, w * 3) != 0)
    {
        R_LOG_ERROR("Failed to update RGB texture: %s", SDL_GetError());
        SDL_DestroyTexture(tex->_sdl_texture);
        tex->_sdl_texture = nullptr;
        return nullptr;
    }

    return tex;
}

std::shared_ptr<texture> texture::create_streaming(
    SDL_Renderer* renderer,
    uint16_t w,
    uint16_t h,
    bool rgba)
{
    if (!renderer || w == 0 || h == 0)
    {
        R_LOG_ERROR("Invalid parameters for create_streaming");
        return nullptr;
    }

    auto tex = std::make_shared<texture>();
    tex->_renderer = renderer;
    tex->_width = w;
    tex->_height = h;
    tex->_is_rgba = rgba;

    Uint32 format = rgba ? SDL_PIXELFORMAT_ABGR8888 : SDL_PIXELFORMAT_RGB24;

    // Create SDL texture with streaming access for frequent updates
    tex->_sdl_texture = SDL_CreateTexture(
        renderer,
        format,
        SDL_TEXTUREACCESS_STREAMING,
        w,
        h
    );

    if (!tex->_sdl_texture)
    {
        R_LOG_ERROR("Failed to create streaming texture: %s", SDL_GetError());
        return nullptr;
    }

    if (rgba)
    {
        SDL_SetTextureBlendMode(tex->_sdl_texture, SDL_BLENDMODE_BLEND);
    }

    return tex;
}

bool texture::update_rgba(const uint8_t* pixels, uint16_t w, uint16_t h)
{
    if (!_sdl_texture || !pixels)
    {
        R_LOG_ERROR("Invalid texture or pixel data in update_rgba");
        return false;
    }

    // If dimensions changed, recreate the texture
    if (w != _width || h != _height)
    {
        SDL_DestroyTexture(_sdl_texture);

        _sdl_texture = SDL_CreateTexture(
            _renderer,
            SDL_PIXELFORMAT_ABGR8888,
            SDL_TEXTUREACCESS_STREAMING,
            w,
            h
        );

        if (!_sdl_texture)
        {
            R_LOG_ERROR("Failed to recreate RGBA texture: %s", SDL_GetError());
            _width = 0;
            _height = 0;
            return false;
        }

        SDL_SetTextureBlendMode(_sdl_texture, SDL_BLENDMODE_BLEND);
        _width = w;
        _height = h;
    }

    if (SDL_UpdateTexture(_sdl_texture, nullptr, pixels, w * 4) != 0)
    {
        R_LOG_ERROR("Failed to update RGBA texture: %s", SDL_GetError());
        return false;
    }

    return true;
}

bool texture::update_rgb(const uint8_t* pixels, uint16_t w, uint16_t h)
{
    if (!_sdl_texture || !pixels)
    {
        R_LOG_ERROR("Invalid texture or pixel data in update_rgb");
        return false;
    }

    // If dimensions changed, recreate the texture
    if (w != _width || h != _height)
    {
        SDL_DestroyTexture(_sdl_texture);

        _sdl_texture = SDL_CreateTexture(
            _renderer,
            SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING,
            w,
            h
        );

        if (!_sdl_texture)
        {
            R_LOG_ERROR("Failed to recreate RGB texture: %s", SDL_GetError());
            _width = 0;
            _height = 0;
            return false;
        }

        _width = w;
        _height = h;
    }

    if (SDL_UpdateTexture(_sdl_texture, nullptr, pixels, w * 3) != 0)
    {
        R_LOG_ERROR("Failed to update RGB texture: %s", SDL_GetError());
        return false;
    }

    return true;
}
