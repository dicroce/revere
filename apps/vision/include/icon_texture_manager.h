#ifndef __vision_icon_texture_manager_h
#define __vision_icon_texture_manager_h

#include <GLFW/glfw3.h>
#include <string>

// Get texture ID for an analytics detection class name
// Returns 0 if no texture exists for the given class
GLuint get_icon_texture_id(const std::string& class_name);

// Initialize icon textures (call after OpenGL context is created)
void init_icon_textures();

#endif
