
#ifndef __r_ui_utils_theme_h
#define __r_ui_utils_theme_h

#include "imgui/imgui.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <string>

// Dark-theme tokens from the Revere Design System (project/tokens.json). Use these names
// instead of literal colors so every surface stays on the same ramp.

namespace r_ui_utils
{

namespace theme
{

constexpr ImVec4 hex(uint32_t rgb, float a = 1.0f)
{
    return ImVec4(((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, a);
}

constexpr ImVec4 with_alpha(const ImVec4& c, float a)
{
    return ImVec4(c.x, c.y, c.z, a);
}

// Zinc ramp, the only neutral.
constexpr ImVec4 neutral_0 = hex(0xffffff);
constexpr ImVec4 neutral_50 = hex(0xfafafa);
constexpr ImVec4 neutral_100 = hex(0xf4f4f5);
constexpr ImVec4 neutral_200 = hex(0xe4e4e7);
constexpr ImVec4 neutral_300 = hex(0xd4d4d8);
constexpr ImVec4 neutral_400 = hex(0xa1a1aa);
constexpr ImVec4 neutral_500 = hex(0x71717a);
constexpr ImVec4 neutral_600 = hex(0x52525b);
constexpr ImVec4 neutral_700 = hex(0x3f3f46);
constexpr ImVec4 neutral_800 = hex(0x27272a);
constexpr ImVec4 neutral_900 = hex(0x18181b);
constexpr ImVec4 neutral_950 = hex(0x09090b);

constexpr ImVec4 blue_300 = hex(0x93c5fd);
constexpr ImVec4 blue_400 = hex(0x60a5fa);
constexpr ImVec4 blue_500 = hex(0x3b82f6);

constexpr ImVec4 ink = hex(0x18181b);
constexpr ImVec4 paper = hex(0xffffff);

// Surfaces
constexpr ImVec4 surface_page = neutral_950;
constexpr ImVec4 surface_card = neutral_900;
constexpr ImVec4 surface_raised = neutral_800;
constexpr ImVec4 surface_sunken = neutral_950;
constexpr ImVec4 surface_overlay = hex(0x000000, 0.6f);
constexpr ImVec4 surface_media = hex(0x000000);

// Text
constexpr ImVec4 text_primary = neutral_50;
constexpr ImVec4 text_secondary = neutral_300;
constexpr ImVec4 text_tertiary = neutral_400;
constexpr ImVec4 text_muted = neutral_500; // decorative only, fails AA
constexpr ImVec4 text_inverse = neutral_900;
constexpr ImVec4 text_on_primary = neutral_900;
constexpr ImVec4 text_on_destructive = hex(0xffffff);

// Accent: links, focus ring, active nav/tab rule, selection, live indicator. Not buttons.
constexpr ImVec4 accent = blue_400;
constexpr ImVec4 text_link = accent;

// Borders
constexpr ImVec4 border_subtle = neutral_800;
constexpr ImVec4 border_default = neutral_700;
constexpr ImVec4 border_strong = neutral_600;
constexpr ImVec4 border_focus = accent;

// Actions
constexpr ImVec4 action_primary = paper;
constexpr ImVec4 action_primary_hover = neutral_200;
constexpr ImVec4 action_primary_active = neutral_300;
constexpr ImVec4 action_destructive = hex(0xdc2626);
constexpr ImVec4 action_destructive_hover = hex(0xb91c1c);

// Status: text on bg, plus a dot. Never colour alone.
constexpr ImVec4 status_success_text = hex(0x34d399);
constexpr ImVec4 status_success_bg = hex(0x10b981, 0.15f);
constexpr ImVec4 status_success_dot = hex(0x10b981);
constexpr ImVec4 status_warning_text = hex(0xfbbf24);
constexpr ImVec4 status_warning_bg = hex(0xf59e0b, 0.15f);
constexpr ImVec4 status_warning_dot = hex(0xf59e0b);
constexpr ImVec4 status_danger_text = hex(0xf87171);
constexpr ImVec4 status_danger_bg = hex(0xef4444, 0.15f);
constexpr ImVec4 status_danger_dot = hex(0xef4444);
constexpr ImVec4 status_neutral_text = hex(0xa1a1aa);
constexpr ImVec4 status_neutral_bg = hex(0x71717a, 0.15f);
constexpr ImVec4 status_neutral_dot = hex(0x71717a);
constexpr ImVec4 status_info_text = hex(0x60a5fa);
constexpr ImVec4 status_info_bg = hex(0x3b82f6, 0.15f);
constexpr ImVec4 status_info_dot = hex(0x3b82f6);

constexpr ImVec4 solid_danger = hex(0xdc2626);
constexpr ImVec4 solid_success = hex(0x059669);
constexpr ImVec4 solid_warning = hex(0xd97706);

// Detection classes (not status). Key by class, never by index.
constexpr ImVec4 class_person = hex(0x3b82f6);
constexpr ImVec4 class_vehicle = hex(0x10b981);
constexpr ImVec4 class_animal = hex(0xf59e0b);
constexpr ImVec4 class_package = hex(0x8b5cf6);
constexpr ImVec4 class_other = hex(0x71717a);
constexpr ImVec4 motion = hex(0xf97316, 0.6f);

// Detection class name (COCO labels from the analytics plugins) -> class family color.
inline ImVec4 detection_class_color(const std::string& class_name)
{
    if(class_name == "person")
        return class_person;
    if(class_name == "bicycle" || class_name == "car" || class_name == "motorcycle" || class_name == "airplane" ||
       class_name == "bus" || class_name == "train" || class_name == "truck" || class_name == "boat")
        return class_vehicle;
    if(class_name == "bird" || class_name == "cat" || class_name == "dog" || class_name == "horse" ||
       class_name == "sheep" || class_name == "cow" || class_name == "bear")
        return class_animal;
    if(class_name == "backpack" || class_name == "handbag" || class_name == "suitcase")
        return class_package;
    return class_other;
}

}

// Replaces ImGui::StyleColorsDark(): design-system colors, square corners, subtle borders.
// Takes the style explicitly: imgui is linked statically, so this DLL has its own (null)
// GImGui and must not call ImGui::GetStyle() itself. Pass ImGui::GetStyle() from the app.
R_API void apply_revere_style(ImGuiStyle& style);

}

#endif
