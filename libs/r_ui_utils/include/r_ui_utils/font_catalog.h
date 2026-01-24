
#ifndef __r_ui_utils_font_catalog_h
#define __r_ui_utils_font_catalog_h

#include "imgui/imgui.h"
#include "r_utils/r_macro.h"
#include <unordered_map>
#include <string>

namespace r_ui_utils
{

struct font_catalog
{
    ImFont* roboto_black;
    ImFont* roboto_black_italic;
    ImFont* roboto_bold;
    ImFont* roboto_bold_italic;
    ImFont* roboto_italic;
    ImFont* roboto_light;
    ImFont* roboto_light_italic;
    ImFont* roboto_medium;
    ImFont* roboto_medium_italic;
    ImFont* roboto_regular;
    ImFont* roboto_thin;
    ImFont* roboto_thin_italic;
};

extern std::unordered_map<std::string, font_catalog> fonts;

R_API void load_fonts(ImGuiIO& io, float size, std::unordered_map<std::string, font_catalog>& fonts);

}

#endif
