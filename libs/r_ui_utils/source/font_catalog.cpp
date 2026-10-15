
#include "r_ui_utils/font_catalog.h"
#include "r_utils/r_string_utils.h"
#include "inter_tight.c"

using namespace std;
using namespace r_utils;
using namespace r_ui_utils;


void r_ui_utils::load_fonts(ImGuiIO& io, float size, std::unordered_map<std::string, font_catalog>& fonts)
{
    font_catalog f;
    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;

    f.inter_tight_regular = io.Fonts->AddFontFromMemoryTTF(InterTight_Regular_ttf, InterTight_Regular_ttf_len, size, &font_cfg);
    f.inter_tight_semibold = io.Fonts->AddFontFromMemoryTTF(InterTight_SemiBold_ttf, InterTight_SemiBold_ttf_len, size, &font_cfg);
    f.inter_tight_bold = io.Fonts->AddFontFromMemoryTTF(InterTight_Bold_ttf, InterTight_Bold_ttf_len, size, &font_cfg);

    auto key = r_string_utils::float_to_s(size, 2);

    fonts[key] = f;
}
