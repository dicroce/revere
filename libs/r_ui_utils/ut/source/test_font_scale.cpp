#include "test_font_scale.h"
#include "r_ui_utils/font_catalog.h"
#include <cmath>
#include <string>
#include <unordered_map>

using namespace r_ui_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_font_scale);

namespace
{

// An ImGui context with the 18 and 14 px fonts made for a display scale, measuring with the 18.
struct fonts_at
{
    ImGuiContext* ctx;
    unordered_map<string, font_catalog> fonts;

    explicit fonts_at(float scale) : ctx(ImGui::CreateContext())
    {
        // CreateContext leaves the one before current.
        ImGui::SetCurrentContext(ctx);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        load_fonts(io, 18.0f, scale, fonts);
        load_fonts(io, 14.0f, scale, fonts);
        build_fonts(io, scale);
    }
    ~fonts_at() { ImGui::DestroyContext(ctx); }

    ImFont* f18() { return fonts["18.00"].inter_tight_regular; }

    ImVec2 measure(const char* text)
    {
        return f18()->CalcTextSizeA(f18()->FontSize, 10000.0f, 0.0f, text);
    }

    // A glyph's height in the atlas's texels.
    float texels_high(ImWchar c)
    {
        const ImFontGlyph* g = f18()->FindGlyph(c);
        return (g->V1 - g->V0) * (float)f18()->ContainerAtlas->TexHeight;
    }
};

// Not "near": windows.h defines it as a macro.
bool near_enough(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

}

void test_font_scale::test_text_measures_the_same_at_any_scale()
{
    // Made at 150% or 200%, a font is still 18 points, and a line measures within a point of its width at 100%: the
    // layout, in points, doesn't change with the display.
    const char* line = "Keep only motion in older video";
    fonts_at one(1.0f);
    const ImVec2 at_1 = one.measure(line);
    for(float scale : {1.5f, 2.0f})
    {
        fonts_at s(scale);
        RTF_ASSERT(s.f18()->FontSize == 18.0f);
        RTF_ASSERT(s.fonts["14.00"].inter_tight_bold->FontSize == 14.0f);
        const ImVec2 at_s = s.measure(line);
        RTF_ASSERT(at_s.y == 18.0f);
        RTF_ASSERT(near_enough(at_s.x, at_1.x, 1.0f));
        // The tab, made from the space by the lookup table, is in points too.
        RTF_ASSERT(near_enough(s.f18()->FindGlyph('\t')->AdvanceX, 4.0f * s.f18()->FindGlyph(' ')->AdvanceX, 0.01f));
        RTF_ASSERT(near_enough(s.f18()->FallbackAdvanceX, s.f18()->FallbackGlyph->AdvanceX, 0.01f));
        // Its ascent and a glyph's box too.
        RTF_ASSERT(near_enough(s.f18()->Ascent, one.f18()->Ascent, 1.0f));
        const ImFontGlyph* h1 = one.f18()->FindGlyph('H');
        const ImFontGlyph* hs = s.f18()->FindGlyph('H');
        RTF_ASSERT(near_enough(hs->Y1 - hs->Y0, h1->Y1 - h1->Y0, 1.0f));
        RTF_ASSERT(near_enough(hs->AdvanceX, h1->AdvanceX, 0.5f));
    }
}

void test_font_scale::test_glyphs_have_the_displays_pixels()
{
    // At 200% each glyph has twice the texels of 100%, so drawn at the display's pixels it isn't stretched.
    fonts_at one(1.0f);
    fonts_at two(2.0f);
    const float h1 = one.texels_high('H');
    const float h2 = two.texels_high('H');
    RTF_ASSERT(h1 > 0.0f);
    RTF_ASSERT(near_enough(h2, 2.0f * h1, 2.0f));
}

void test_font_scale::test_scale_1_is_as_before()
{
    // Scale 1 (macOS and Linux) is load_fonts without one, and an atlas built as ImGui builds it.
    fonts_at one(1.0f);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    unordered_map<string, font_catalog> plain;
    load_fonts(ImGui::GetIO(), 18.0f, plain);
    ImGui::GetIO().Fonts->Build();
    ImFont* p = plain["18.00"].inter_tight_regular;
    const ImVec2 b = p->CalcTextSizeA(p->FontSize, 10000.0f, 0.0f, "Front door");
    const ImVec2 a = one.measure("Front door");
    RTF_ASSERT(p->FontSize == one.f18()->FontSize);
    RTF_ASSERT(a.x == b.x && a.y == b.y);
    RTF_ASSERT(p->Ascent == one.f18()->Ascent);
    ImGui::DestroyContext(ctx);
}

void test_font_scale::test_the_display_scale()
{
    // A window's pixels over its points, to the nearest hundredth.
    RTF_ASSERT(display_scale(1500, 1000) == 1.5f);
    RTF_ASSERT(display_scale(1251, 1001) == 1.25f);     // a point rounded the other way is still 125%
    RTF_ASSERT(display_scale(1000, 1000) == 1.0f);
    RTF_ASSERT(display_scale(2200, 1000) == 2.2f);
    // Never under 1 or over 4, and 1 without a size.
    RTF_ASSERT(display_scale(900, 1000) == 1.0f);
    RTF_ASSERT(display_scale(5000, 1000) == 4.0f);
    RTF_ASSERT(display_scale(0, 1000) == 1.0f);
    RTF_ASSERT(display_scale(1500, 0) == 1.0f);
}
