
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
    ImFont* inter_tight_regular;
    ImFont* inter_tight_semibold;
    ImFont* inter_tight_bold;
};

extern std::unordered_map<std::string, font_catalog> fonts;

R_API void load_fonts(ImGuiIO& io, float size, std::unordered_map<std::string, font_catalog>& fonts);

}

#endif
