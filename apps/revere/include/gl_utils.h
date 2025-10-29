
#ifndef __revere_gl_utils_h
#define __revere_gl_utils_h

#ifdef IS_WINDOWS
#include <windows.h>
#endif

#include <GLFW/glfw3.h> // Will drag system OpenGL headers
#include <string>

namespace revere
{

void load_texture_from_image_file(const std::string& filename, GLuint* out_texture, int* out_width, int* out_height);

void load_texture_from_image_memory(const uint8_t* data, size_t size, GLuint* out_texture, int* out_width, int* out_height);

GLuint load_texture_from_rgba(const uint8_t* pixels, size_t size, uint16_t w, uint16_t h);

}

#endif
