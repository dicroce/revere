#ifndef __vision_icon_texture_manager_h
#define __vision_icon_texture_manager_h

#include <string>

// Get texture ID for an analytics detection class name
// Returns nullptr if no texture exists for the given class
void* get_icon_texture_id(const std::string& class_name);

// Initialize icon textures (call after SDL renderer is created)
void init_icon_textures();

#endif
