
#include "gl_utils.h"
#include "r_utils/r_exception.h"
#include "error_handling.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define GL_CLAMP_TO_EDGE 0x812F 

using namespace std;
using namespace r_utils;

// Simple helper function to load an image into a OpenGL texture with common settings
void vision::load_texture_from_file(const string& filename, GLuint* out_texture, int* out_width, int* out_height)
{
    // Input validation
    if (!out_texture || !out_width || !out_height)
    {
        R_LOG_ERROR("NULL output parameters in load_texture_from_file");
        return;
    }
    
    if (filename.empty())
    {
        R_LOG_ERROR("Empty filename in load_texture_from_file");
        return;
    }
    
    // Load from file
    int image_width = 0;
    int image_height = 0;
    int channels = 0;
    unsigned char* image_data = stbi_load(filename.c_str(), &image_width, &image_height, &channels, 4);
    if (image_data == NULL)
    {
        R_LOG_ERROR("Failed to load image file: %s", filename.c_str());
        return;
    }
    
    // Validate frame dimensions
    if (!state_validate::is_valid_frame_dimensions(static_cast<uint16_t>(image_width), static_cast<uint16_t>(image_height)))
    {
        R_LOG_ERROR("Invalid image dimensions from file %s: %dx%d", filename.c_str(), image_width, image_height);
        stbi_image_free(image_data);
        return;
    }
    
    // Validate buffer size (4 channels for RGBA)
    size_t expected_size = static_cast<size_t>(image_width) * image_height * 4;
    if (!state_validate::is_valid_buffer_size(expected_size, static_cast<uint16_t>(image_width), static_cast<uint16_t>(image_height), 4))
    {
        R_LOG_ERROR("Invalid buffer size for image %s", filename.c_str());
        stbi_image_free(image_data);
        return;
    }

    // Create texture using safe method
    GLuint image_texture = 0;
    if (!gl_safe::create_texture(&image_texture, image_width, image_height, image_data))
    {
        R_LOG_ERROR("Failed to create OpenGL texture for file %s", filename.c_str());
        stbi_image_free(image_data);
        return;
    }
    
    stbi_image_free(image_data);

    *out_texture = image_texture;
    *out_width = image_width;
    *out_height = image_height;
}

GLuint vision::load_texture_from_rgba(const uint8_t* pixels, size_t buffer_size, uint16_t w, uint16_t h)
{
    // Input validation
    if (!pixels)
    {
        R_LOG_ERROR("NULL pixel data in load_texture_from_rgba");
        return 0;
    }
    
    // Validate frame dimensions
    if (!state_validate::is_valid_frame_dimensions(w, h))
    {
        R_LOG_ERROR("Invalid frame dimensions in load_texture_from_rgba: %dx%d", w, h);
        return 0;
    }
    
    // Validate buffer size (4 channels for RGBA)
    if (!state_validate::is_valid_buffer_size(buffer_size, w, h, 4))
    {
        R_LOG_ERROR("Invalid buffer size in load_texture_from_rgba: %zu bytes for %dx%d", buffer_size, w, h);
        return 0;
    }

    // Create texture using safe method
    GLuint image_texture = 0;
    if (!gl_safe::create_texture(&image_texture, static_cast<int>(w), static_cast<int>(h), pixels))
    {
        R_LOG_ERROR("Failed to create OpenGL texture from RGBA data");
        return 0;
    }

    return image_texture;
}