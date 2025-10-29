#ifndef __r_ui_utils_svg_icons_h
#define __r_ui_utils_svg_icons_h

#include "r_utils/r_macro.h"
#include <vector>
#include <cstdint>

namespace r_ui_utils
{

// SVG icon definitions
namespace svg_icons {
    // Person standing icon
    constexpr const char* person = R"(
        <svg width="24" height="24" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <circle cx="12" cy="5" r="3" fill="currentColor"/>
            <path d="M12 9 C10 9 8 10 8 12 L8 18 L10 18 L10 22 L14 22 L14 18 L16 18 L16 12 C16 10 14 9 12 9 Z" fill="currentColor"/>
        </svg>
    )";
    
    // Car icon
    constexpr const char* car = R"(
        <svg width="24" height="24" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <path d="M5 11 L3 11 C2 11 2 12 2 12 L2 16 C2 17 3 17 3 17 L21 17 C22 17 22 16 22 16 L22 12 C22 11 21 11 21 11 L19 11 L18 8 C18 7 17 7 17 7 L7 7 C6 7 6 8 6 8 L5 11 Z" fill="currentColor"/>
            <circle cx="6" cy="18" r="2" fill="currentColor"/>
            <circle cx="18" cy="18" r="2" fill="currentColor"/>
            <rect x="8" y="9" width="3" height="2" fill="white"/>
            <rect x="13" y="9" width="3" height="2" fill="white"/>
        </svg>
    )";
}

// Structure to hold rendered icon bitmap
struct icon_bitmap {
    std::vector<uint8_t> pixels;  // RGBA data
    int width;
    int height;
};

// Render an SVG string to a bitmap at specified size
R_API icon_bitmap render_svg_to_bitmap(const char* svg_string, int size, uint32_t color = 0x000000FF);

// Pre-render common icons at a specific size
struct icon_cache {
    icon_bitmap person;
    icon_bitmap car;
};

R_API icon_cache create_icon_cache(int size, uint32_t color = 0x000000FF);

}

#endif