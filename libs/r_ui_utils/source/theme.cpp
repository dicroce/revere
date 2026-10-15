#include "r_ui_utils/theme.h"

using namespace r_ui_utils;
using namespace r_ui_utils::theme;

void r_ui_utils::apply_revere_style(ImGuiStyle& style)
{
    ImGui::StyleColorsDark(&style);

    // Corners are square (Decisions 7).
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    const ImVec4 clear = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    ImVec4* c = style.Colors;

    c[ImGuiCol_Text] = text_primary;
    c[ImGuiCol_TextDisabled] = text_muted;

    c[ImGuiCol_WindowBg] = surface_page;
    c[ImGuiCol_ChildBg] = clear;
    c[ImGuiCol_PopupBg] = surface_raised;
    // border-default: inputs get a visible outline, and it matches the button fill so
    // buttons don't show a darker ring inside their edge.
    c[ImGuiCol_Border] = border_default;
    c[ImGuiCol_BorderShadow] = clear;

    // Inputs sit on surface-sunken with a border.
    c[ImGuiCol_FrameBg] = surface_sunken;
    c[ImGuiCol_FrameBgHovered] = surface_card;
    c[ImGuiCol_FrameBgActive] = surface_card;

    c[ImGuiCol_TitleBg] = surface_card;
    c[ImGuiCol_TitleBgActive] = surface_card;
    c[ImGuiCol_TitleBgCollapsed] = surface_card;
    c[ImGuiCol_MenuBarBg] = surface_card;

    c[ImGuiCol_ScrollbarBg] = clear;
    c[ImGuiCol_ScrollbarGrab] = border_default;
    c[ImGuiCol_ScrollbarGrabHovered] = border_strong;
    c[ImGuiCol_ScrollbarGrabActive] = neutral_500;

    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = blue_300;

    // Blue is the accent, not the button: buttons step along the neutral ramp.
    // Two steps above surface-card so buttons read as controls on every surface
    // (neutral-800 was nearly invisible against the page and cards).
    c[ImGuiCol_Button] = neutral_700;
    c[ImGuiCol_ButtonHovered] = neutral_600;
    c[ImGuiCol_ButtonActive] = neutral_500;

    // Header = selected rows, menu hover, collapsing headers.
    c[ImGuiCol_Header] = with_alpha(accent, 0.20f);
    // Translucent so a Selectable drawn over other widgets (the camera cards draw their
    // buttons first, then a full-card Selectable) doesn't paint over them on hover.
    c[ImGuiCol_HeaderHovered] = with_alpha(neutral_50, 0.06f);
    c[ImGuiCol_HeaderActive] = with_alpha(neutral_50, 0.10f);

    c[ImGuiCol_Separator] = border_subtle;
    c[ImGuiCol_SeparatorHovered] = border_strong;
    c[ImGuiCol_SeparatorActive] = accent;

    c[ImGuiCol_ResizeGrip] = clear;
    c[ImGuiCol_ResizeGripHovered] = border_strong;
    c[ImGuiCol_ResizeGripActive] = accent;

    c[ImGuiCol_Tab] = surface_card;
    c[ImGuiCol_TabHovered] = neutral_800;
    c[ImGuiCol_TabActive] = neutral_800;
    c[ImGuiCol_TabUnfocused] = surface_card;
    c[ImGuiCol_TabUnfocusedActive] = neutral_800;

    c[ImGuiCol_PlotLines] = text_tertiary;
    c[ImGuiCol_PlotLinesHovered] = accent;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotHistogramHovered] = blue_300;

    c[ImGuiCol_TableHeaderBg] = surface_card;
    c[ImGuiCol_TableBorderStrong] = border_default;
    c[ImGuiCol_TableBorderLight] = border_subtle;
    c[ImGuiCol_TableRowBg] = clear;
    c[ImGuiCol_TableRowBgAlt] = with_alpha(surface_card, 0.6f);

    c[ImGuiCol_TextSelectedBg] = with_alpha(accent, 0.35f);
    c[ImGuiCol_DragDropTarget] = accent;
    c[ImGuiCol_NavHighlight] = border_focus;
    c[ImGuiCol_NavWindowingHighlight] = with_alpha(paper, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = surface_overlay;
    c[ImGuiCol_ModalWindowDimBg] = surface_overlay;
}
