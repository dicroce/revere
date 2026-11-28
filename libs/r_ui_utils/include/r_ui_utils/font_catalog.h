
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

// Fonts made sharp for a display with `scale` pixels to a point (1.5 at Windows' 150%), where the window and its
// layout are in points and the renderer draws them at the display's pixels. Each font is rasterized at size * scale,
// keyed by its size as with load_fonts. Call build_fonts with the same scale once they're all added.
R_API void load_fonts(ImGuiIO& io, float size, float scale, std::unordered_map<std::string, font_catalog>& fonts);

// Builds the atlas, then puts each font's measurements back in points (divides them by scale), so text measures and
// lays out as at scale 1 while each glyph's texture has the display's pixels.
R_API void build_fonts(ImGuiIO& io, float scale);

// The scale fonts are made for, from a window's size in pixels and in points: to the nearest hundredth (Windows'
// scales are whole percents), so a resize that rounds a point differently doesn't remake them; 1 to 4.
R_API float display_scale(int pixels, int points);

}

#endif
