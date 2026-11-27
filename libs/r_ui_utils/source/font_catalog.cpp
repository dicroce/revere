
#include "r_ui_utils/font_catalog.h"
#include "r_utils/r_string_utils.h"
#include "inter_tight.c"
#include <algorithm>
#include <cmath>

using namespace std;
using namespace r_utils;
using namespace r_ui_utils;


void r_ui_utils::load_fonts(ImGuiIO& io, float size, std::unordered_map<std::string, font_catalog>& fonts)
{
    load_fonts(io, size, 1.0f, fonts);
}

void r_ui_utils::load_fonts(ImGuiIO& io, float size, float scale, std::unordered_map<std::string, font_catalog>& fonts)
{
    font_catalog f;
    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;

    const float pixels = size * scale;
    f.inter_tight_regular = io.Fonts->AddFontFromMemoryTTF(InterTight_Regular_ttf, InterTight_Regular_ttf_len, pixels, &font_cfg);
    f.inter_tight_semibold = io.Fonts->AddFontFromMemoryTTF(InterTight_SemiBold_ttf, InterTight_SemiBold_ttf_len, pixels, &font_cfg);
    f.inter_tight_bold = io.Fonts->AddFontFromMemoryTTF(InterTight_Bold_ttf, InterTight_Bold_ttf_len, pixels, &font_cfg);

    auto key = r_string_utils::float_to_s(size, 2);

    fonts[key] = f;
}

void r_ui_utils::build_fonts(ImGuiIO& io, float scale)
{
    io.Fonts->Build();
    if(scale == 1.0f)
        return;
    // The glyphs' texture stays at the display's pixels; their sizes and positions go to points.
    const float k = 1.0f / scale;
    for(ImFont* font : io.Fonts->Fonts)
    {
        font->FontSize *= k;
        font->Ascent *= k;
        font->Descent *= k;
        for(ImFontGlyph& g : font->Glyphs)
        {
            g.X0 *= k;
            g.Y0 *= k;
            g.X1 *= k;
            g.Y1 *= k;
            g.AdvanceX *= k;
        }
        // The advances it looks up, the tab and the fallback, from the glyphs again.
        font->BuildLookupTable();
    }
}

float r_ui_utils::display_scale(int pixels, int points)
{
    if(pixels <= 0 || points <= 0)
        return 1.0f;
    const float s = std::round((float)pixels * 100.0f / (float)points) / 100.0f;
    return std::clamp(s, 1.0f, 4.0f);
}
