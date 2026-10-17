
#ifndef __revere_imgui_ui_h
#define __revere_imgui_ui_h

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/ImGuiFileDialog.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include "r_storage/r_storage_file.h"
#include "r_disco/r_camera.h"
#include "r_vss/r_camera_stats.h"
#include <map>
#include <memory>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <cstdint>
#include "assignment_state.h"
#include "rtsp_source_camera_config.h"
#include "gl_utils.h"
#include "r_ui_utils/font_catalog.h"
#include "r_ui_utils/theme.h"
#include "r_ui_utils/utils.h"

namespace revere
{

// What the desktop app calls the password for revere's local web interface
// (stored as the "system password"; the stored name doesn't change). One place,
// so the wording can change without hunting for strings.
namespace web_password_text
{
inline constexpr const char* name = "Local web password";
inline constexpr const char* menu_item = "Local web password...";
inline constexpr const char* description = "Protects Revere's web interface on your local network. Anyone opening it will need this password.";
inline constexpr const char* password_label = "Password";
inline constexpr const char* confirm_label = "Confirm password";
inline constexpr const char* error_empty = "Enter a password.";
inline constexpr const char* error_mismatch = "The passwords don't match.";
inline constexpr const char* save = "Save";
inline constexpr const char* cancel = "Cancel";
inline constexpr const char* remove = "Remove password";
inline constexpr const char* remove_confirm = "Remove the local web password? The web interface will ask for a new one the next time it's opened.";
inline constexpr const char* remove_confirm_yes = "Remove";
inline constexpr const char* remove_confirm_no = "Keep password";
}

// BeginPopupModal with a taller title bar. ImGui sizes the title bar from
// FramePadding at Begin, so it's raised just for that call; the dialog's
// own content keeps the normal padding.
inline bool begin_modal(const char* name, ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 9.0f));
    bool open = ImGui::BeginPopupModal(name, nullptr, flags);
    ImGui::PopStyleVar();
    return open;
}

// The menu bar's spacing: taller than a frame elsewhere, wider gaps between
// menus, no box around the bar (a hairline under it instead, drawn by
// main_menu), and menus with the same roomier items. Every BeginMainMenuBar()
// needs the same push, or the bar's height changes within a frame.
inline void push_menu_bar_style()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(16.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
}

inline void pop_menu_bar_style()
{
    ImGui::PopStyleVar(4);
}

template<typename EXIT_CB, typename MINIMIZE_CB, typename ADD_RTSP_SOURCE_CAMERA_CB, typename LAUNCH_VISION_CB, typename DOWNLOAD_REVERE_CLOUD_CB, typename GET_STARTUP_STATE_CB, typename SET_STARTUP_STATE_CB, typename OPEN_WEB_UI_CB, typename SET_SYSTEM_PASSWORD_CB>
uint16_t main_menu(EXIT_CB exit_cb, MINIMIZE_CB minimize_cb, ADD_RTSP_SOURCE_CAMERA_CB add_rtsp_source_camera_cb, LAUNCH_VISION_CB launch_vision_cb, DOWNLOAD_REVERE_CLOUD_CB download_revere_cloud_cb, GET_STARTUP_STATE_CB get_startup_state_cb, SET_STARTUP_STATE_CB set_startup_state_cb, OPEN_WEB_UI_CB open_web_ui_cb, SET_SYSTEM_PASSWORD_CB set_system_password_cb)
{
    push_menu_bar_style();
    ImGui::BeginMainMenuBar();
    {
        auto* dl = ImGui::GetForegroundDrawList();
        auto p = ImGui::GetWindowPos();
        auto sz = ImGui::GetWindowSize();
        dl->AddLine(ImVec2(p.x, p.y + sz.y - 1.0f), ImVec2(p.x + sz.x, p.y + sz.y - 1.0f), ImGui::GetColorU32(r_ui_utils::theme::border_subtle));
    }
    if (ImGui::BeginMenu("Cameras"))
    {
        if (ImGui::MenuItem("Add RTSP camera..."))
            add_rtsp_source_camera_cb();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            exit_cb();

        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem("Launch Vision"))
            launch_vision_cb();
        if (ImGui::MenuItem("Open web UI"))
            open_web_ui_cb();
        if (ImGui::MenuItem("Minimize to system tray"))
            minimize_cb();

        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Settings"))
    {
        bool startup_enabled = get_startup_state_cb();
        if (ImGui::MenuItem("Start Revere at login", nullptr, &startup_enabled))
            set_startup_state_cb(startup_enabled);

        if (ImGui::MenuItem(web_password_text::menu_item))
            set_system_password_cb();

        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Revere Cloud"))
    {
        if (ImGui::MenuItem("About Revere Cloud"))
            download_revere_cloud_cb();

        ImGui::EndMenu();
    }
    uint16_t h = (uint16_t)ImGui::GetWindowHeight();
    ImGui::EndMainMenuBar();
    pop_menu_bar_style();
    return h;
}

template<typename MAIN_CB>
void main_layout(
    uint16_t client_top,
    uint16_t window_width,
    uint16_t window_height,
    MAIN_CB main_cb,
    r_utils::r_nullable<std::string> status_text,
    const std::string& font_key_18 = "18.00"
)
{
    ImGui::SetNextWindowPos(ImVec2(0, client_top));
    uint16_t status_height = (uint16_t)(ImGui::GetTextLineHeightWithSpacing()*2);
    uint16_t main_height = window_height - status_height;
    ImGui::SetNextWindowSize(ImVec2(window_width, main_height));
    ImGui::Begin("##main");
    main_cb(0, client_top, window_width, main_height);
    ImGui::End();

    bool open = true;
    uint16_t line_height = (uint16_t)ImGui::GetTextLineHeightWithSpacing();
    ImVec4 status_bar_bg = r_ui_utils::theme::surface_page;
    ImGui::PushFont(r_ui_utils::fonts[font_key_18].inter_tight_regular);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, status_bar_bg);
    ImGui::SetNextWindowPos(ImVec2(0, (float)(window_height - line_height)));
    ImGui::SetNextWindowSize(ImVec2(window_width, line_height));
    ImGui::Begin("##status", &open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    if(!status_text.is_null())
    {
        ImGui::SetCursorPos(ImVec2(10, 5));
        ImGui::Text("%s",status_text.value().c_str());
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

template<typename FIRST_CB, typename SECOND_CB, typename THIRD_CB>
void thirds(
    uint16_t x,
    uint16_t y,
    uint16_t window_width,
    uint16_t window_height,
    const std::string& first_label,
    FIRST_CB first_cb,
    const std::string& second_label,
    SECOND_CB second_cb,
    const std::string& third_label,
    THIRD_CB third_cb
)
{
    // Side panels are cards; the middle panel is the page. border-subtle separates them.
    ImVec4 side_panel_bg = r_ui_utils::theme::surface_card;
    ImVec4 middle_panel_bg = r_ui_utils::theme::surface_page;

    uint16_t panel_width = window_width / 3;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, side_panel_bg);
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(panel_width, window_height));
    ImGui::Begin(first_label.c_str());
    first_cb(panel_width);
    ImGui::End();
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_WindowBg, middle_panel_bg);
    ImGui::SetNextWindowPos(ImVec2((float)(x + panel_width), (float)y));
    ImGui::SetNextWindowSize(ImVec2(panel_width, window_height));
    ImGui::Begin(second_label.c_str());
    second_cb(panel_width);
    ImGui::End();
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_WindowBg, side_panel_bg);
    ImGui::SetNextWindowPos(ImVec2(x + ((float)panel_width*2), y));
    ImGui::SetNextWindowSize(ImVec2(panel_width, window_height));
    ImGui::Begin(third_label.c_str());
    third_cb(panel_width);
    ImGui::End();
    ImGui::PopStyleColor();
}

template<typename FIRST_CB, typename SECOND_CB, typename THIRD_CB, typename LOG_CB>
void thirds_with_log(
    uint16_t x,
    uint16_t y,
    uint16_t window_width,
    uint16_t window_height,
    const std::string& first_label,
    FIRST_CB first_cb,
    const std::string& second_label,
    SECOND_CB second_cb,
    const std::string& third_label,
    THIRD_CB third_cb,
    const std::string& log_label,
    LOG_CB log_cb,
    float& log_height
)
{
    // Clamp log_height to reasonable bounds
    float min_log_height = 100.0f;
    float max_log_height = window_height - 200.0f;
    if(log_height < min_log_height) log_height = min_log_height;
    if(log_height > max_log_height) log_height = max_log_height;

    // Calculate heights for top and bottom sections
    float top_height = window_height - log_height;

    // Top section: three columns
    // Side panels are cards; the middle panel is the page. border-subtle separates them.
    ImVec4 side_panel_bg = r_ui_utils::theme::surface_card;
    ImVec4 middle_panel_bg = r_ui_utils::theme::surface_page;

    uint16_t panel_width = window_width / 3;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, side_panel_bg);
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(panel_width, top_height));
    ImGui::Begin(first_label.c_str());
    first_cb(panel_width);
    ImGui::End();
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_WindowBg, middle_panel_bg);
    ImGui::SetNextWindowPos(ImVec2((float)(x + panel_width), (float)y));
    ImGui::SetNextWindowSize(ImVec2(panel_width, top_height));
    ImGui::Begin(second_label.c_str());
    second_cb(panel_width);
    ImGui::End();
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_WindowBg, side_panel_bg);
    ImGui::SetNextWindowPos(ImVec2(x + ((float)panel_width*2), y));
    ImGui::SetNextWindowSize(ImVec2(panel_width, top_height));
    ImGui::Begin(third_label.c_str());
    third_cb(panel_width);
    ImGui::End();
    ImGui::PopStyleColor();

    // Bottom section: full width log (resizable from top)
    ImVec4 log_panel_bg = r_ui_utils::theme::surface_sunken;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, log_panel_bg);
    ImGui::SetNextWindowPos(ImVec2(x, y + top_height));
    ImGui::SetNextWindowSize(ImVec2(window_width, log_height));
    ImGui::Begin(log_label.c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // Update log_height if window was resized
    log_height = ImGui::GetWindowHeight();

    log_cb(window_width, (uint16_t)log_height);
    ImGui::End();
    ImGui::PopStyleColor();
}

enum class stream_health
{
    unknown,        // no status data available
    healthy,        // receiving video frames
    not_receiving,  // stream up but no video frames (status: warning)
    failed          // recording context failed to start (status: danger)
};

struct sidebar_list_ui_item
{
    std::string label;
    std::string sub_label;
    std::string camera_id;
    std::string kbps;
    std::string retention;
    stream_health health {stream_health::unknown};
    // Optional per-card action button (e.g. "Repair" when the camera's
    // recording file is missing). Empty = no button.
    std::string action_label;

    // Shown in the main window's detail pane when known.
    std::string codec;
    std::string resolution;     // "2560 × 1920"
    std::string frame_rate;     // "15 fps"
    r_utils::r_nullable<bool> motion_detection;

    // The camera pane's stats tiles (project-planning#105): the last read of
    // the camera's storage (null until one lands), and stream reconnects in
    // the last 24 h, counted in memory since reconnects_counted_since.
    std::shared_ptr<const r_vss::camera_stats> stats;
    size_t reconnects_24h {0};
    std::chrono::system_clock::time_point reconnects_counted_since;
};

template<typename BUTTON_CLICK_CB, typename ITEM_CLICK_CB, typename FORGET_BUTTON_CLICK_CB, typename PROPERTIES_CLICK_CB, typename VIEW_CLICK_CB, typename ACTION_CLICK_CB>
void sidebar_list(
    ImGuiContext*,
    revere::assignment_state&,
    int selected,
    uint16_t width,
    std::vector<sidebar_list_ui_item>& labels,
    const std::string& button_label,
    BUTTON_CLICK_CB button_click_cb,
    ITEM_CLICK_CB item_click_cb,
    bool show_forget_button,
    FORGET_BUTTON_CLICK_CB forget_button_click_cb,
    bool show_properties_button,
    PROPERTIES_CLICK_CB properties_click_cb,
    bool show_view_button,
    VIEW_CLICK_CB view_click_cb,
    ACTION_CLICK_CB action_click_cb,   // fired for cards whose action_label is set
    float& cached_largest_label,
    const std::string& font_key_24 = "24.00",
    const std::string& font_key_22 = "22.00",
    const std::string& font_key_18 = "18.00"
)
{
    auto pos = ImGui::GetCursorPos();
    const float start_x = pos.x;
    const float start_y = pos.y;

    // Only calculate text sizes if cache is invalid (< 0) or list changed
    if(cached_largest_label < 0.0f)
    {
        float largest_label = 0;
        for(int i = 0; i < (int)labels.size(); ++i)
        {
            auto label_size = ImGui::CalcTextSize(labels[i].label.c_str());
            auto sub_label_size = ImGui::CalcTextSize(labels[i].sub_label.c_str());

            if(label_size.x > largest_label)
                largest_label = label_size.x;
            if(sub_label_size.x > largest_label)
                largest_label = sub_label_size.x;
        }
        cached_largest_label = largest_label;
    }

    // Store text rendering info for batching
    struct TextRenderInfo {
        ImVec2 position;
        std::string text;
        bool is_bold;
    };
    std::vector<TextRenderInfo> text_to_render;
    text_to_render.reserve(labels.size() * 2);

    // Card layout. Recording cards have two extra rows (bitrate / retention)
    // tucked between the IP and the buttons, so they're taller. Discovered
    // cards have no stats at all — using the tall layout there leaves a big
    // empty gap between the IP and the Record button. Detect which list this
    // is by looking for any stats anywhere in the list, and pick the layout
    // once for the whole list (mixed pitch in one list would break alignment).
    bool any_has_stats = false;
    for(const auto& lbl : labels)
    {
        if(!lbl.kbps.empty() || !lbl.retention.empty())
        {
            any_has_stats = true;
            break;
        }
    }
    const int card_height = any_has_stats ? 140 : 92;   // selectable height
    const int card_pitch  = any_has_stats ? 150 : 102;  // 10px gap between cards
    const int label_y     = 5;
    const int sub_label_y = 30;
    const int bitrate_y   = 54;
    const int retention_y = 74;
    // Leave a little room for the retention line (~20px tall at 18pt) before the buttons.
    const int buttons_y   = any_has_stats ? 101 : 54;

    // selectable list - first pass: interactive elements
    for(int i = 0; i < (int)labels.size(); ++i)
    {
        ImGui::PushID(i);

        auto name = r_utils::r_string_utils::format("##Object %d", i);

        // Note: Though the button is actually inside the selectable it has to come first because otherwise
        // the Selectable will steal the click event.
        // Smaller font + tighter buttons so a full row (e.g. Remove/Properties/
        // View) fits within the card width.
        ImFont* btn_font = r_ui_utils::fonts[font_key_18].inter_tight_semibold;
        if(btn_font)
            ImGui::PushFont(btn_font);
        const int button_width = 72;
        const int button_height = 28;
        // 72px minimum; labels that don't fit (e.g. "Properties") grow to fit with 10px each side.
        auto btn_size = [&](const char* label) {
            return ImVec2(std::max((float)button_width, ImGui::CalcTextSize(label).x + 20.0f), (float)button_height);
        };
        ImGui::SetCursorPos(ImVec2(pos.x+10, pos.y + buttons_y));
        if(ImGui::Button(button_label.c_str(), btn_size(button_label.c_str())))
        {
            item_click_cb(i);
            button_click_cb(i);
        }

        if(show_forget_button)
        {
            ImGui::SameLine();
            if(ImGui::Button("Forget", btn_size("Forget")))
                forget_button_click_cb(i);
        }

        if(show_properties_button)
        {
            ImGui::SameLine();
            if(ImGui::Button("Properties", btn_size("Properties")))
                properties_click_cb(i);
        }

        if(show_view_button)
        {
            ImGui::SameLine();
            if(ImGui::Button("View", btn_size("View")))
                view_click_cb(i);
        }

        if(!labels[i].action_label.empty())
        {
            ImGui::SameLine();
            if(ImGui::Button(labels[i].action_label.c_str(), btn_size(labels[i].action_label.c_str())))
                action_click_cb(i);
        }

        if(btn_font)
            ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(pos.x, pos.y));
        // Selection, hover and focus colors come from the theme (accent tint when selected).
        if (ImGui::Selectable(name.c_str(), i == selected, 0, ImVec2((float)width, (float)card_height))) {
            item_click_cb(i);
        }

        // GetCursorScreenPos() here allows the IMDrawList to scroll when the containing
        // window scrolls.
        auto cursor_pos = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddLine(ImVec2(cursor_pos.x, cursor_pos.y), ImVec2(cursor_pos.x + width, cursor_pos.y), ImGui::GetColorU32(r_ui_utils::theme::border_subtle));

        // Store text for batched rendering
        text_to_render.push_back({ImVec2(pos.x+10, pos.y + label_y), labels[i].label, true});
        text_to_render.push_back({ImVec2(pos.x+10, pos.y + sub_label_y), labels[i].sub_label, false});

        pos.y += card_pitch;

        ImGui::PopID();
    }

    // Second pass: batch render all text with same font together
    ImGui::PushFont(r_ui_utils::fonts[font_key_24].inter_tight_semibold);
    for(const auto& text_info : text_to_render)
    {
        if(text_info.is_bold)
        {
            ImGui::SetCursorPos(text_info.position);
            ImGui::Text("%s", text_info.text.c_str());
        }
    }
    ImGui::PopFont();

    ImGui::PushFont(r_ui_utils::fonts[font_key_22].inter_tight_regular);
    for(const auto& text_info : text_to_render)
    {
        if(!text_info.is_bold)
        {
            ImGui::SetCursorPos(text_info.position);
            ImGui::Text("%s", text_info.text.c_str());
        }
    }
    ImGui::PopFont();

    // Third pass: render labeled bitrate / retention rows. These replace the old
    // green/red health-dot indicator. If stream_health is error, the text is
    // rendered in red so the "something is wrong" signal is preserved.
    //
    // The rows are left-aligned below the IP address so they don't collide with
    // the camera name when the sidebar is narrow.
    ImGui::PushFont(r_ui_utils::fonts[font_key_18].inter_tight_regular);
    for(size_t i = 0; i < labels.size(); ++i)
    {
        const auto& lbl = labels[i];
        if(lbl.kbps.empty() && lbl.retention.empty())
            continue;

        float item_y = start_y + (float)i * (float)card_pitch;

        // Status is never colour alone: a problem gets a dot plus a word on the first row.
        const bool failed = (lbl.health == stream_health::failed);
        const bool not_receiving = (lbl.health == stream_health::not_receiving);
        const ImVec4 secondary = r_ui_utils::theme::text_secondary;

        auto status_row = [&](const ImVec4& dot, const ImVec4& text, const std::string& line) {
            ImGui::SetCursorPos(ImVec2(start_x + 10.0f, item_y + (float)bitrate_y));
            auto p = ImGui::GetCursorScreenPos();
            float r = 4.0f;  // 8px status dot
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f), r, ImGui::GetColorU32(dot));
            ImGui::SetCursorPos(ImVec2(start_x + 10.0f + 2.0f * r + 6.0f, item_y + (float)bitrate_y));
            ImGui::TextColored(text, "%s", line.c_str());
        };

        if(failed)
            status_row(r_ui_utils::theme::status_danger_dot, r_ui_utils::theme::status_danger_text, "Failed to start");
        else if(not_receiving)
            status_row(r_ui_utils::theme::status_warning_dot, r_ui_utils::theme::status_warning_text, "No video Â· bitrate: " + lbl.kbps);
        else if(!lbl.kbps.empty())
        {
            ImGui::SetCursorPos(ImVec2(start_x + 10.0f, item_y + (float)bitrate_y));
            ImGui::TextColored(secondary, "bitrate: %s", lbl.kbps.c_str());
        }

        if(!lbl.retention.empty())
        {
            ImGui::SetCursorPos(ImVec2(start_x + 10.0f, item_y + (float)retention_y));
            if(failed)
                ImGui::TextColored(r_ui_utils::theme::status_danger_text, "%s", lbl.retention.c_str());
            else ImGui::TextColored(secondary, "retention: %s", lbl.retention.c_str());
        }
    }
    ImGui::PopFont();
}

template<typename OK_CB>
void download_revere_cloud_modal(
    ImGuiContext*,
    const std::string& name,
    OK_CB ok_cb
)
{
    if (begin_modal(name.c_str()))
    {
        ImGui::Text("We opened a browser with the download links for the Revere Cloud version you need.");
        ImGui::Text("Restart Revere after you install Revere Cloud.");

        ImGui::Spacing();

        if(ImGui::Button("Ok"))
        {
            ok_cb();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// Revere Cloud's Terms of Use and Privacy Policy. Connecting a gateway to
// Revere Cloud is where this open-source software meets the hosted service,
// so the notice sits at that step.
inline constexpr const char* REVERE_CLOUD_TERMS_URL = "https://revere.video/terms";
inline constexpr const char* REVERE_CLOUD_PRIVACY_URL = "https://revere.video/privacy";

// Text that acts like a link: hand cursor, underline and the URL as a tooltip
// on hover, open_cb(url) on click.
template<typename OPEN_CB>
void link_text(const char* label, const char* url, const ImVec4& color, OPEN_CB open_cb)
{
    ImGui::TextColored(color, "%s", label);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), max, ImGui::GetColorU32(color));
        ImGui::SetTooltip("%s", url);
    }
    if (ImGui::IsItemClicked())
        open_cb(std::string(url));
}

// "By connecting to Revere Cloud you agree to the Terms of Use and Privacy
// Policy", with both linked. Flowed word by word so it wraps to whatever width
// the plugins panel has; each link stays one unbroken, clickable piece.
template<typename OPEN_CB>
void revere_cloud_terms_notice(OPEN_CB open_cb)
{
    const ImVec4 text_color = r_ui_utils::theme::text_tertiary;
    const ImVec4 link_color = r_ui_utils::theme::text_link;

    struct piece { std::string text; const char* url; };
    std::vector<piece> pieces;
    auto add_words = [&](const std::string& text) {
        size_t i = 0;
        while(i < text.size())
        {
            size_t j = text.find(' ', i);
            size_t end = (j == std::string::npos) ? text.size() : j + 1;  // keep the trailing space
            pieces.push_back({text.substr(i, end - i), nullptr});
            i = end;
        }
    };
    add_words("By connecting to Revere Cloud you agree to the ");
    pieces.push_back({"Terms of Use", REVERE_CLOUD_TERMS_URL});
    add_words(" and ");
    pieces.push_back({"Privacy Policy", REVERE_CLOUD_PRIVACY_URL});
    add_words(".");

    const float right = ImGui::GetContentRegionMax().x;
    bool first = true;
    for(const auto& p : pieces)
    {
        if(!first)
        {
            ImGui::SameLine(0.0f, 0.0f);
            // Measure without a trailing space: a line may end just past the edge with one.
            std::string fit = p.text;
            while(!fit.empty() && fit.back() == ' ')
                fit.pop_back();
            if(ImGui::GetCursorPosX() + ImGui::CalcTextSize(fit.c_str()).x > right)
                ImGui::NewLine();
        }
        first = false;

        if(p.url)
            link_text(p.text.c_str(), p.url, link_color, open_cb);
        else ImGui::TextColored(text_color, "%s", p.text.c_str());
    }
}

template<typename DOWNLOAD_CB, typename CLOSE_CB>
void about_revere_cloud_modal(
    ImGuiContext*,
    const std::string& name,
    DOWNLOAD_CB download_cb,
    CLOSE_CB close_cb
)
{
    ImGui::SetNextWindowSize(ImVec2(640, 0));
    if (begin_modal(name.c_str()))
    {
        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::SetCursorPosX(16.0f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 608.0f);
        ImGui::TextWrapped("Revere Cloud is an optional plugin for Revere that connects your system to the Revere Cloud infrastructure and enables the Lantern mobile app to access your cameras remotely. Some Revere Cloud features require paying for service (and some do not).");
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float button_width = 260.0f;
        float total_buttons = button_width * 2 + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX((640.0f - total_buttons) * 0.5f);

        if(ImGui::Button("Download Revere Cloud", ImVec2(button_width, 0)))
        {
            download_cb();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if(ImGui::Button("Close", ImVec2(button_width, 0)))
        {
            close_cb();
            ImGui::CloseCurrentPopup();
        }

        ImGui::Spacing();

        ImGui::EndPopup();
    }
}

}

#endif
