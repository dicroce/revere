
#include "gl_utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_std_utils.h"

#include "r_ui_utils/stb_image.h"

#define GL_CLAMP_TO_EDGE 0x812F 

using namespace std;
using namespace r_utils;
using namespace r_utils::r_std_utils;

void revere::load_texture_from_image_file(const string& filename, GLuint* out_texture, int* out_width, int* out_height)
{
    int channels = 0;
    raii_ptr<unsigned char> image_data(stbi_load(filename.c_str(), out_width, out_height, &channels, 3), stbi_image_free);
    if(image_data.get() == nullptr)
        R_THROW(("Unable to load image!"));

    // Create a OpenGL texture identifier
    glGenTextures(1, out_texture);
    glBindTexture(GL_TEXTURE_2D, *out_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

    // Upload pixels into texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, *out_width, *out_height, 0, GL_RGB, GL_UNSIGNED_BYTE, image_data.get());
}

void revere::load_texture_from_image_memory(const uint8_t* data, size_t size, GLuint* out_texture, int* out_width, int* out_height)
{
    int channels = 0;
    raii_ptr<unsigned char> image_data(stbi_load_from_memory(data, (int)size, out_width, out_height, &channels, 3), stbi_image_free);
    if(image_data.get() == nullptr)
        R_THROW(("Unable to load image!"));

    glGenTextures(1, out_texture);
    glBindTexture(GL_TEXTURE_2D, *out_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

    // Upload pixels into texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, *out_width, *out_height, 0, GL_RGB, GL_UNSIGNED_BYTE, image_data.get());
}

GLuint revere::load_texture_from_rgba(const uint8_t* pixels, size_t, uint16_t w, uint16_t h)
{
    // Create a OpenGL texture identifier
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    return image_texture;
}