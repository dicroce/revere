
#ifndef __vision_overlay_controls_h
#define __vision_overlay_controls_h

#include "imgui/imgui.h"
#include "r_ui_utils/theme.h"
#include <algorithm>
#include <cmath>

namespace vision
{

// The View window's corner controls: volume, Keep on top and Close, as small
// icon buttons in one strip at the top right of the video, the way picture in
// picture windows do it. They fade in and out with the timeline (alpha).
struct overlay_controls_result
{
    bool close {false};
    bool toggle_pin {false};
    bool toggle_mute {false};
    // Where the strip is, in window coordinates, so the window's hit test can
    // let clicks there through instead of dragging the window.
    float x {0.0f}, y {0.0f}, w {0.0f}, h {0.0f};
};

namespace overlay_controls_detail
{

constexpr float button = 28.0f;   // square icon button
constexpr float inset = 10.0f;    // from the window's top and right edges
constexpr float pad = 2.0f;       // around the buttons inside the strip
constexpr float slider_w = 72.0f;

inline ImU32 col(const ImVec4& c, float alpha)
{
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

// An icon button: hover background, accent fill when on. Returns clicked.
template<typename DRAW>
bool icon_button(const char* id, bool on, float alpha, const char* tip, DRAW draw)
{
    using namespace r_ui_utils::theme;
    auto p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(button, button));
    bool hovered = ImGui::IsItemHovered();
    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 b(p.x + button, p.y + button);
    if(on)
        dl->AddRectFilled(p, b, col(hovered ? blue_300 : accent, alpha));
    else if(hovered)
        dl->AddRectFilled(p, b, col(surface_raised, alpha));
    const ImVec4& fg = on ? text_on_primary : (hovered ? text_primary : text_secondary);
    draw(dl, ImVec2(p.x + button * 0.5f, p.y + button * 0.5f), col(fg, alpha));
    if(hovered && alpha > 0.5f)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", tip);
    }
    return clicked;
}

inline void draw_close(ImDrawList* dl, ImVec2 c, ImU32 col)
{
    const float r = 6.0f;
    dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 1.75f);
    dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, 1.75f);
}

// Keep on top: a window outline behind, and a solid window in front of it.
inline void draw_on_top(ImDrawList* dl, ImVec2 c, ImU32 col)
{
    dl->AddRect(ImVec2(c.x - 8.0f, c.y - 7.0f), ImVec2(c.x + 3.0f, c.y + 2.0f), col, 0.0f, 0, 1.5f);
    dl->AddRectFilled(ImVec2(c.x - 3.0f, c.y - 2.0f), ImVec2(c.x + 8.0f, c.y + 7.0f), col);
}

// A speaker; waves for the level, or a cross when muted.
inline void draw_speaker(ImDrawList* dl, ImVec2 c, ImU32 col, float level)
{
    const float x = c.x - 7.0f;
    dl->AddRectFilled(ImVec2(x, c.y - 3.0f), ImVec2(x + 4.0f, c.y + 3.0f), col);
    dl->AddTriangleFilled(ImVec2(x + 3.0f, c.y - 3.0f), ImVec2(x + 9.0f, c.y - 7.5f), ImVec2(x + 9.0f, c.y + 7.5f), col);
    dl->AddTriangleFilled(ImVec2(x + 3.0f, c.y - 3.0f), ImVec2(x + 9.0f, c.y + 7.5f), ImVec2(x + 3.0f, c.y + 3.0f), col);
    if(level <= 0.0f)
    {
        dl->AddLine(ImVec2(c.x + 4.0f, c.y - 3.5f), ImVec2(c.x + 9.0f, c.y + 3.5f), col, 1.5f);
        dl->AddLine(ImVec2(c.x + 4.0f, c.y + 3.5f), ImVec2(c.x + 9.0f, c.y - 3.5f), col, 1.5f);
        return;
    }
    const float pi = 3.14159265f;
    dl->PathArcTo(ImVec2(x + 9.0f, c.y), 4.0f, -pi / 4.0f, pi / 4.0f, 8);
    dl->PathStroke(col, 0, 1.5f);
    if(level > 0.5f)
    {
        dl->PathArcTo(ImVec2(x + 9.0f, c.y), 8.0f, -pi / 4.0f, pi / 4.0f, 10);
        dl->PathStroke(col, 0, 1.5f);
    }
}

// The strip's size, without drawing it.
inline float corner_strip_w()
{
    const float gap = 2.0f;
    return pad * 2.0f + button + gap + slider_w + 8.0f + gap + button + gap + button;
}

inline float corner_strip_h()
{
    return pad * 2.0f + button;
}

}

// The narrowest the View window can be and still show the whole strip, with
// its inset on both sides.
inline int overlay_min_width()
{
    using namespace overlay_controls_detail;
    return (int)std::ceil(corner_strip_w() + 2.0f * inset);
}

// The lowest it can be with the strip clear of a time bar time_bar_h high.
inline int overlay_min_height(float time_bar_h)
{
    using namespace overlay_controls_detail;
    return (int)std::ceil(inset + corner_strip_h() + inset + time_bar_h);
}

// Draws the strip and returns what was clicked. volume is the stream's gain
// (0 = silent, up to 4); the slider edits it in place.
inline overlay_controls_result overlay_corner_controls(float win_w, float alpha, bool pinned, float& volume)
{
    using namespace r_ui_utils::theme;
    using namespace overlay_controls_detail;
    overlay_controls_result r;

    const float gap = 2.0f;
    r.w = corner_strip_w();
    r.h = corner_strip_h();
    r.x = win_w - inset - r.w;
    r.y = inset;

    ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
    ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(surface_overlay.x, surface_overlay.y, surface_overlay.z, surface_overlay.w * alpha));
    ImGui::Begin("##overlay_corner", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);

    const float level = std::min(volume / 4.0f, 1.0f);
    if(icon_button("##mute", false, alpha, volume > 0.0f ? "Mute (M)" : "Unmute (M)",
        [&](ImDrawList* dl, ImVec2 c, ImU32 cl) { draw_speaker(dl, c, cl, level); }))
        r.toggle_mute = true;

    // A thin track and a square knob, in the theme's colours.
    ImGui::SameLine();
    {
        auto p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##vol", ImVec2(slider_w + 8.0f, button));
        bool active = ImGui::IsItemActive();
        bool hovered = ImGui::IsItemHovered();
        const float x0 = p.x + 4.0f, x1 = p.x + 4.0f + slider_w;
        if(active)
            volume = std::clamp((ImGui::GetIO().MousePos.x - x0) / slider_w, 0.0f, 1.0f) * 4.0f;
        if(hovered && alpha > 0.5f)
            ImGui::SetTooltip("Volume");
        auto* dl = ImGui::GetWindowDrawList();
        const float cy = p.y + button * 0.5f;
        const float kx = x0 + slider_w * level;
        dl->AddRectFilled(ImVec2(x0, cy - 1.5f), ImVec2(x1, cy + 1.5f), col(border_default, alpha));
        dl->AddRectFilled(ImVec2(x0, cy - 1.5f), ImVec2(kx, cy + 1.5f), col(hovered || active ? text_primary : text_secondary, alpha));
        dl->AddRectFilled(ImVec2(kx - 4.0f, cy - 6.0f), ImVec2(kx + 4.0f, cy + 6.0f), col(text_primary, alpha));
    }

    ImGui::SameLine();
    if(icon_button("##pin", pinned, alpha, pinned ? "Keeping on top. Click to stop (T)" : "Keep on top (T)",
        [&](ImDrawList* dl, ImVec2 c, ImU32 cl) { draw_on_top(dl, c, cl); }))
        r.toggle_pin = true;

    ImGui::SameLine();
    if(icon_button("##close", false, alpha, "Close (Esc)",
        [&](ImDrawList* dl, ImVec2 c, ImU32 cl) { draw_close(dl, c, cl); }))
        r.close = true;

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    return r;
}

}

#endif
