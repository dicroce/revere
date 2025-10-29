#include "r_ui_utils/svg_icons.h"

#ifdef IS_WINDOWS
#pragma warning(push)
#pragma warning(disable: 4244) // possible loss of data
#endif

#define NANOSVG_IMPLEMENTATION
#include "imgui/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "imgui/nanosvgrast.h"

#ifdef IS_WINDOWS
#pragma warning(pop)
#endif

#include <cstring>
#include <string>

using namespace r_ui_utils;

icon_bitmap r_ui_utils::render_svg_to_bitmap(const char* svg_string, int size, uint32_t color)
{
    icon_bitmap result;
    result.width = size;
    result.height = size;
    
    // Replace "currentColor" with actual color
    std::string svg_with_color = svg_string;
    
    // Convert color to hex string (RGBA -> RGB hex)
    char color_hex[8];
    snprintf(color_hex, sizeof(color_hex), "#%02X%02X%02X", 
             (color >> 24) & 0xFF,  // R
             (color >> 16) & 0xFF,  // G
             (color >> 8) & 0xFF);   // B
    
    size_t pos = 0;
    while ((pos = svg_with_color.find("currentColor", pos)) != std::string::npos) {
        svg_with_color.replace(pos, 12, color_hex);
        pos += strlen(color_hex);
    }
    
    // Parse SVG
    NSVGimage* image = nsvgParse(const_cast<char*>(svg_with_color.c_str()), "px", 96.0f);
    if (!image) {
        return result;
    }
    
    // Create rasterizer
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(image);
        return result;
    }
    
    // Allocate pixel buffer (RGBA) - initialize to transparent
    result.pixels.resize(size * size * 4);
    
    // Initialize all pixels to transparent (RGBA: 0,0,0,0)
    std::memset(result.pixels.data(), 0, result.pixels.size());
    
    // Calculate scale to fit the SVG into our target size
    float scale = size / 24.0f; // Our SVGs are 24x24 base size
    
    // Rasterize with transparent background
    nsvgRasterize(rast, image, 0, 0, scale, 
                  result.pixels.data(), size, size, size * 4);
    
    // Clean up
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    
    return result;
}

icon_cache r_ui_utils::create_icon_cache(int size, uint32_t color)
{
    icon_cache cache;
    cache.person = render_svg_to_bitmap(svg_icons::person, size, color);
    cache.car = render_svg_to_bitmap(svg_icons::car, size, color);
    return cache;
}