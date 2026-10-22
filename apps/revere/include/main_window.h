#ifndef __revere_main_window_h
#define __revere_main_window_h

// The main window as a master and detail layout: one list on the left
// (Recording, Discovered and Plugins), the selected item's details and actions
// on the right, an optional log pane at the bottom and a status bar with one
// chip per system plugin.
//
// Everything here draws; the callers own the data and the actions. Camera rows
// are the same sidebar_list_ui_item the old three-panel layout used, and each
// action calls the callback the old cards called.

#include "imgui_ui.h"
#include "r_ui_utils/log_tail.h"
#include "r_ui_utils/text_selection.h"
#include "r_utils/r_qr_code.h"
#include "r_utils/3rdparty/json/json.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace revere
{

// One loaded system plugin, as the plugin reports itself.
struct plugin_view
{
    std::string name;
    std::string display_name;   // the status JSON's "name", the plugin's name for itself; may be empty
    std::string version;        // empty when the plugin doesn't report one
    bool enabled {false};
    bool has_status {false};    // false when the plugin has no status export
    std::string status_label;
    ImVec4 status_color {r_ui_utils::theme::status_neutral_dot};
    std::string status_message; // JSON from system_plugin_get_status_message, may be empty
};

// A plugin's status colour as one of revere's status colours, so a plugin's
// "ok" dot is the same green as a recording camera's. Plugins may send a name
// ("green"/"ok", "yellow"/"amber"/"warning", "red"/"error", "grey"/"neutral"),
// "#rrggbb", or [r, g, b]; a colour is matched by hue, and anything grey,
// dark or outside green/amber/red is neutral.
inline ImVec4 plugin_status_color(const nlohmann::json& c)
{
    using namespace r_ui_utils::theme;
    float r = 0.4f, g = 0.4f, b = 0.4f;
    if(c.is_string())
    {
        auto n = c.get<std::string>();
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
        if(n == "green" || n == "ok" || n == "success") return status_success_dot;
        if(n == "yellow" || n == "amber" || n == "orange" || n == "warn" || n == "warning") return status_warning_dot;
        if(n == "red" || n == "error" || n == "danger") return status_danger_dot;
        if(n.size() != 7 || n[0] != '#' || !std::all_of(n.begin() + 1, n.end(), [](unsigned char ch) { return std::isxdigit(ch); }))
            return status_neutral_dot;
        auto v = std::stoul(n.substr(1), nullptr, 16);
        r = (float)((v >> 16) & 0xff) / 255.0f;
        g = (float)((v >> 8) & 0xff) / 255.0f;
        b = (float)(v & 0xff) / 255.0f;
    }
    else if(c.is_array() && c.size() >= 3 && c[0].is_number() && c[1].is_number() && c[2].is_number())
    {
        auto ch = [&](size_t i) { return (float)std::clamp(c[i].get<int>(), 0, 255) / 255.0f; };
        r = ch(0); g = ch(1); b = ch(2);
    }
    else
        return status_neutral_dot;

    float h, sat, val;
    ImGui::ColorConvertRGBtoHSV(r, g, b, h, sat, val);
    const float deg = h * 360.0f;
    if(sat < 0.25f || val < 0.2f)
        return status_neutral_dot;
    if(deg >= 75.0f && deg < 170.0f)
        return status_success_dot;
    if(deg >= 20.0f && deg < 75.0f)
        return status_warning_dot;
    if(deg < 20.0f || deg >= 330.0f)
        return status_danger_dot;
    return status_neutral_dot;
}

// Fills the status fields of a plugin_view from system_plugin_get_status's JSON:
// {"label": "...", "color": ..., "name": "..."} ("name" is optional; "color"
// as in plugin_status_color). Leaves has_status false if it can't be read.
inline void parse_plugin_status(const std::string& status_json, plugin_view& pv)
{
    if(status_json.empty())
        return;
    try
    {
        auto sj = nlohmann::json::parse(status_json);
        if(sj.contains("name") && sj["name"].is_string())
            pv.display_name = sj["name"].get<std::string>();
        pv.status_label = sj.value("label", std::string("Unknown"));
        pv.status_color = sj.contains("color") ? plugin_status_color(sj["color"]) : r_ui_utils::theme::status_neutral_dot;
        pv.has_status = true;
    }
    catch(...) {}
}

// The log pane's lines (see r_ui_utils/log_tail.h).
using r_ui_utils::log_tail;

struct main_window_state
{
    float list_width {260.0f};
    // Which list sections are open. main.cpp loads these from revere_cfg.json
    // at start and saves them as soon as one changes.
    bool recording_open {true};
    bool discovered_open {true};
    bool plugins_open {true};
    bool show_log {false};
    float log_height {180.0f};
    // Text selected in the log pane, and whether the mouse is dragging it out.
    r_ui_utils::text_selection log_selection;
    bool log_dragging {false};
    std::string selected_plugin;    // empty unless a plugin row is selected
    // False until r_devices' first cache fill; the camera sections say
    // "Loading cameras..." rather than that they're empty.
    bool cameras_loaded {true};

    // A note at the bottom right of the window that fades out by itself (see
    // show_toast). toast_id non-empty offers "Don't show again".
    std::string toast_text;
    std::string toast_id;
    double toast_until {0.0};

    void show_toast(const std::string& text, const std::string& id = std::string(), double seconds = 8.0)
    {
        toast_text = text;
        toast_id = id;
        toast_until = ImGui::GetTime() + seconds;
    }
};

struct main_window_actions
{
    std::function<void(int)> select_recording;
    std::function<void(int)> select_discovered;
    std::function<void()> select_plugin;    // clears the camera selection
    std::function<void(int)> view;
    std::function<void(int)> properties;
    std::function<void(int)> remove;
    std::function<void(int)> repair;        // the card's action_label button
    std::function<void(int)> record;
    std::function<void(int)> forget;
    std::function<void(const std::string&, bool)> set_plugin_enabled;
    std::function<void(const std::string&)> open_url;
    std::function<void()> add_rtsp_camera;
    std::function<void(const std::string&)> dismiss_notice;  // "Don't show again" on a toast
};

struct main_window_fonts
{
    std::string key_24;
    std::string key_22;
    std::string key_18;
};

// "<name>_system_plugin" (or "lib<name>_system_plugin") reads as "<name>" in
// the list, the status bar and About Revere.
inline std::string plugin_short_name(const std::string& name)
{
    const std::string suffix = "_system_plugin";
    if(name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
        return name;
    auto short_name = name.substr(0, name.size() - suffix.size());
    // A library file name's "lib" prefix (as on Linux) isn't part of the name.
    if(short_name.size() > 3 && short_name.compare(0, 3, "lib") == 0)
        short_name.erase(0, 3);
    return short_name;
}

// The plugin's own name for itself when it gives one, else its short name.
inline std::string plugin_display_name(const plugin_view& p)
{
    return p.display_name.empty() ? plugin_short_name(p.name) : p.display_name;
}

// "0.0.30-4-gabc1234-dirty" reads as "0.0.30": git describe's commit count,
// hash and dirty suffixes dropped.
inline std::string short_version(std::string v)
{
    const std::string dirty = "-dirty";
    if(v.size() > dirty.size() && v.compare(v.size() - dirty.size(), dirty.size(), dirty) == 0)
        v.resize(v.size() - dirty.size());
    auto g = v.rfind("-g");
    if(g != std::string::npos && g + 2 < v.size() &&
       std::all_of(v.begin() + g + 2, v.end(), [](unsigned char c) { return std::isxdigit(c); }))
    {
        auto n = v.rfind('-', g - 1);
        if(n != std::string::npos && n + 1 < g &&
           std::all_of(v.begin() + n + 1, v.begin() + g, [](unsigned char c) { return std::isdigit(c); }))
            v.resize(n);
    }
    return v;
}

namespace main_window_detail
{

inline ImFont* font(const std::string& key, bool semibold = false)
{
    auto& f = r_ui_utils::fonts[key];
    return semibold ? f.inter_tight_semibold : f.inter_tight_regular;
}

inline std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

// Lays items out left to right and starts a new line when the next one doesn't
// fit, so a row of buttons wraps instead of running off the pane.
struct flow_row
{
    float right;
    bool first {true};

    flow_row() : right(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x) {}

    // After text that wrapped, the next item starts a new line rather than
    // sitting beside the first line of it.
    void after_text()
    {
        if(ImGui::GetItemRectSize().y > ImGui::GetTextLineHeightWithSpacing())
            first = true;
    }

    void next(float item_width, bool align_right = false)
    {
        if(!first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + item_width <= right)
        {
            ImGui::SameLine();
            if(align_right)
                ImGui::SetCursorScreenPos(ImVec2(right - item_width, ImGui::GetCursorScreenPos().y));
        }
        first = false;
    }
};

enum class button_kind { normal, primary, ghost, ghost_destructive, destructive };

inline ImVec2 button_size(const char* label, bool small = false)
{
    auto t = ImGui::CalcTextSize(label, nullptr, true);
    auto pad = ImGui::GetStyle().FramePadding;
    return small ? ImVec2(t.x + pad.x * 2.0f, t.y) : ImVec2(t.x + pad.x * 2.0f, t.y + pad.y * 2.0f);
}

// A button in one of the kinds above.
inline bool styled_button(const char* label, button_kind kind = button_kind::normal, bool small = false)
{
    using namespace r_ui_utils::theme;
    int pushed = 0;
    auto push = [&](ImGuiCol idx, const ImVec4& c) { ImGui::PushStyleColor(idx, c); ++pushed; };
    switch(kind)
    {
        case button_kind::primary:
            push(ImGuiCol_Button, accent);
            push(ImGuiCol_ButtonHovered, blue_300);
            push(ImGuiCol_ButtonActive, blue_500);
            push(ImGuiCol_Text, text_on_primary);
            break;
        case button_kind::ghost:
            push(ImGuiCol_Button, with_alpha(surface_raised, 0.0f));
            push(ImGuiCol_ButtonHovered, surface_raised);
            push(ImGuiCol_ButtonActive, border_default);
            push(ImGuiCol_Text, text_secondary);
            break;
        case button_kind::ghost_destructive:
            push(ImGuiCol_Button, with_alpha(surface_raised, 0.0f));
            push(ImGuiCol_ButtonHovered, status_danger_bg);
            push(ImGuiCol_ButtonActive, status_danger_bg);
            push(ImGuiCol_Text, status_danger_text);
            break;
        case button_kind::destructive:
            push(ImGuiCol_Button, action_destructive);
            push(ImGuiCol_ButtonHovered, action_destructive_hover);
            push(ImGuiCol_ButtonActive, action_destructive_hover);
            push(ImGuiCol_Text, text_on_destructive);
            break;
        default:
            break;
    }
    // Primary and destructive are one solid fill and the ghosts have no box
    // at rest, so only normal buttons keep the outline.
    bool borderless = kind != button_kind::normal;
    if(borderless)
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    bool clicked = small ? ImGui::SmallButton(label) : ImGui::Button(label);
    if(borderless)
        ImGui::PopStyleVar();
    ImGui::PopStyleColor(pushed);
    return clicked;
}

// align_right: when the button fits on the current line, put it at the far
// right of the row (Remove, away from the safe actions); otherwise it wraps
// like any other item.
inline bool flow_button(flow_row& row, const char* label, button_kind kind = button_kind::normal, bool small = false, bool align_right = false)
{
    row.next(button_size(label, small).x, align_right);
    return styled_button(label, kind, small);
}

// The main actions of a detail pane: roomier buttons than the global frame
// padding (which tables and inputs share), wider gaps between them, and space
// above and below the row.
struct action_row
{
    flow_row row;

    action_row()
    {
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, ImGui::GetStyle().ItemSpacing.y));
    }
    ~action_row()
    {
        ImGui::PopStyleVar(2);
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }
    action_row(const action_row&) = delete;
    action_row& operator=(const action_row&) = delete;
};

inline void wrapped_text(const ImVec4& color, const std::string& text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// Text clipped with "..." to max_width, drawn at pos.
inline void ellipsis_text(ImDrawList* dl, ImVec2 pos, float max_width, const ImVec4& color, const std::string& text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    auto t = ImGui::CalcTextSize(text.c_str());
    ImVec2 min(pos.x, pos.y);
    ImVec2 max(pos.x + max_width, pos.y + t.y);
    ImGui::RenderTextEllipsis(dl, min, max, max.x, max.x, text.c_str(), nullptr, &t);
    ImGui::PopStyleColor();
}

// The local web UI opened on one camera (its ?camera= deep link).
inline std::string local_web_ui_camera_url(const std::string& camera_id)
{
    std::string encoded;
    for(unsigned char c : camera_id)
    {
        if(std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            encoded += (char)c;
        else
        {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            encoded += buf;
        }
    }
    return "http://localhost:8088/?camera=" + encoded;
}

inline ImVec4 camera_dot(stream_health h)
{
    using namespace r_ui_utils::theme;
    switch(h)
    {
        case stream_health::healthy: return status_success_dot;
        case stream_health::not_receiving: return status_warning_dot;
        case stream_health::failed: return status_danger_dot;
        default: return status_neutral_dot;
    }
}

inline const char* camera_state(stream_health h)
{
    switch(h)
    {
        case stream_health::healthy: return "recording";
        case stream_health::not_receiving: return "not receiving video";
        case stream_health::failed: return "failed to start";
        default: return "starting";
    }
}

// A section header that opens and closes its section: arrow, title and count.
// The count shows either way, so a closed section still says what's in it.
inline void section_header(const std::string& id, const std::string& title, size_t count, bool& open, const main_window_fonts& fonts)
{
    using namespace r_ui_utils::theme;
    ImGui::PushFont(font(fonts.key_18));
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    const float h = ImGui::GetFontSize() + 10.0f;
    const float width = ImGui::GetContentRegionAvail().x;
    if(ImGui::InvisibleButton(id.c_str(), ImVec2(width, h)))
        open = !open;
    const bool hovered = ImGui::IsItemHovered();
    auto min = ImGui::GetItemRectMin();
    auto max = ImGui::GetItemRectMax();
    auto* dl = ImGui::GetWindowDrawList();
    if(hovered)
        dl->AddRectFilled(min, max, ImGui::GetColorU32(surface_raised));

    const ImU32 color = ImGui::GetColorU32(hovered ? text_secondary : text_tertiary);
    const float fs = ImGui::GetFontSize();
    const float arrow_scale = 0.7f;
    const float ty = min.y + (h - fs) * 0.5f;
    ImGui::RenderArrow(dl, ImVec2(min.x + 10.0f, ty + fs * (1.0f - arrow_scale) * 0.5f), color, open ? ImGuiDir_Down : ImGuiDir_Right, arrow_scale);
    std::string label = upper(title) + " \xC2\xB7 " + std::to_string(count);
    dl->AddText(ImVec2(min.x + 10.0f + fs * arrow_scale + 6.0f, ty), color, label.c_str());
    ImGui::PopFont();
}

inline void empty_state(const std::string& text, const main_window_fonts& fonts)
{
    ImGui::PushFont(font(fonts.key_18));
    ImGui::Indent(10.0f);
    wrapped_text(r_ui_utils::theme::text_tertiary, text);
    ImGui::Unindent(10.0f);
    ImGui::PopFont();
}

// One selectable list row: status dot, name, and right_text right-aligned when
// there's room for it after the name. Returns true when clicked.
inline bool list_row(const std::string& id, bool selected, const ImVec4& dot, const std::string& name, const std::string& right_text, const main_window_fonts& fonts)
{
    using namespace r_ui_utils::theme;
    ImFont* name_font = font(fonts.key_22, true);
    ImFont* right_font = font(fonts.key_18);
    const float row_h = name_font->FontSize + 16.0f;
    const float width = ImGui::GetContentRegionAvail().x;

    bool clicked = ImGui::Selectable(id.c_str(), selected, 0, ImVec2(width, row_h));
    auto min = ImGui::GetItemRectMin();
    auto max = ImGui::GetItemRectMax();
    auto* dl = ImGui::GetWindowDrawList();

    if(selected)
        dl->AddRectFilled(min, ImVec2(min.x + 2.0f, max.y), ImGui::GetColorU32(accent));

    const float pad = 10.0f;
    const float r = 4.0f;
    const float cy = (min.y + max.y) * 0.5f;
    dl->AddCircleFilled(ImVec2(min.x + pad + r, cy), r, ImGui::GetColorU32(dot));

    const float text_x = min.x + pad + r * 2.0f + 10.0f;
    const float text_right = max.x - pad;

    float right_w = 0.0f;
    if(!right_text.empty())
    {
        ImGui::PushFont(right_font);
        right_w = ImGui::CalcTextSize(right_text.c_str()).x;
        ImGui::PopFont();
    }

    ImGui::PushFont(name_font);
    const float name_w = ImGui::CalcTextSize(name.c_str()).x;
    const bool show_right = right_w > 0.0f && text_x + name_w + 16.0f + right_w <= text_right;
    ellipsis_text(dl, ImVec2(text_x, cy - name_font->FontSize * 0.5f), std::max(0.0f, text_right - text_x), text_primary, name);
    ImGui::PopFont();

    if(show_right)
    {
        ImGui::PushFont(right_font);
        ImGui::PushStyleColor(ImGuiCol_Text, text_tertiary);
        dl->AddText(ImVec2(text_right - right_w, cy - right_font->FontSize * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), right_text.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    else if(!right_text.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", right_text.c_str());

    return clicked;
}

// One tile of a stat_cards grid: a small uppercase label over a value, with an
// optional detail line under the value and an optional hover tooltip. A card
// can also carry a 24-hour strip under the value, or a bar per detection class
// in place of it (the camera pane's stats).
struct stat_card
{
    std::string label;
    std::string value;
    std::string detail;
    std::string hint;
    const std::array<int, 24>* hours {nullptr}; // per-hour counts drawn under the value
    // Counts by detection group, most first. When set and not empty, the card
    // takes a row of its own and draws a bar per group instead of the value.
    const std::vector<std::pair<std::string, int>>* classes {nullptr};
};

// A detection group's colour (r_vss::detection_group's names), the same one the
// overlay and timeline use for its classes.
inline ImVec4 detection_group_color(const std::string& group)
{
    using namespace r_ui_utils::theme;
    if(group == "vehicle")
        return class_vehicle;
    if(group == "animal")
        return class_animal;
    return detection_class_color(group);
}

// Small labelled values in a grid that fits as many columns of at least
// min_width as the pane allows. Cards in a row are as tall as the tallest, so
// rows line up; a row holding an hour strip is that much taller, and a card
// with classes spans its row and is as tall as its bars.
inline void stat_cards(const std::vector<stat_card>& stats, const main_window_fonts& fonts)
{
    using namespace r_ui_utils::theme;
    if(stats.empty())
        return;

    const float gap = 10.0f;
    const float pad_x = 10.0f;
    const float pad_y = 8.0f;
    const float strip_h = 12.0f;
    const float bar_h = 10.0f;
    const size_t max_class_lines = 4; // more than this: the top 3 and "N more"
    ImFont* key_font = font(fonts.key_18);
    ImFont* value_font = font(fonts.key_22, true);
    // Measured as ellipsis_text measures (CalcTextSize under the font, which
    // rounds up), so "cut" agrees with whether an ellipsis was drawn.
    auto text_w = [](ImFont* f, const std::string& t) {
        ImGui::PushFont(f);
        float w = ImGui::CalcTextSize(t.c_str()).x;
        ImGui::PopFont();
        return w;
    };
    auto wide = [](const stat_card& c) { return c.classes && !c.classes->empty(); };
    auto class_lines = [&](const stat_card& c) {
        return c.classes->size() <= max_class_lines ? c.classes->size() : max_class_lines;
    };

    // A value too long for its card ends in an ellipsis and shows in full on hover.
    const float min_width = 130.0f;
    bool any_detail = std::any_of(stats.begin(), stats.end(), [&](const stat_card& c) { return !wide(c) && !c.detail.empty(); });
    const float detail_h = any_detail ? 2.0f + key_font->FontSize : 0.0f;
    const float base_h = pad_y + key_font->FontSize + 4.0f + value_font->FontSize + detail_h + pad_y;
    const float class_line_h = std::max(key_font->FontSize, bar_h) + 6.0f;

    const float avail = ImGui::GetContentRegionAvail().x;
    const int narrow = (int)std::count_if(stats.begin(), stats.end(), [&](const stat_card& c) { return !wide(c); });
    int cols = std::max(1, (int)((avail + gap) / (min_width + gap)));
    cols = std::max(1, std::min(cols, narrow));
    const float card_w = (avail - gap * (float)(cols - 1)) / (float)cols;

    // Place the cards: narrow ones fill the grid left to right, a wide one
    // starts a row and takes all of it.
    struct place { int row; int col; int span; };
    std::vector<place> places;
    int row = 0, col = 0;
    for(const auto& c : stats)
    {
        if(wide(c))
        {
            if(col > 0)
                ++row;
            places.push_back({row++, 0, cols});
            col = 0;
        }
        else
        {
            places.push_back({row, col, 1});
            if(++col == cols)
            {
                ++row;
                col = 0;
            }
        }
    }
    const int rows = col > 0 ? row + 1 : row;

    std::vector<float> row_h((size_t)rows, base_h), row_y((size_t)rows, 0.0f);
    for(size_t i = 0; i < stats.size(); ++i)
    {
        auto& h = row_h[(size_t)places[i].row];
        if(wide(stats[i]))
            h = std::max(h, pad_y + key_font->FontSize + 6.0f + (float)class_lines(stats[i]) * class_line_h + pad_y - 6.0f);
        else if(stats[i].hours)
            h = std::max(h, base_h + 6.0f + strip_h);
    }
    for(int r = 1; r < rows; ++r)
        row_y[(size_t)r] = row_y[(size_t)r - 1] + row_h[(size_t)r - 1] + gap;

    auto origin = ImGui::GetCursorScreenPos();
    auto* dl = ImGui::GetWindowDrawList();
    int hovered = -1;
    bool hovered_cut = false;
    std::string hovered_full;
    for(size_t i = 0; i < stats.size(); ++i)
    {
        const auto& c = stats[i];
        const auto& p = places[i];
        const float w = card_w * (float)p.span + gap * (float)(p.span - 1);
        ImVec2 a(origin.x + (float)p.col * (card_w + gap), origin.y + row_y[(size_t)p.row]);
        ImVec2 b(a.x + w, a.y + row_h[(size_t)p.row]);
        dl->AddRectFilled(a, b, ImGui::GetColorU32(surface_card));
        dl->AddRect(a, b, ImGui::GetColorU32(border_subtle));

        const float inner_w = w - pad_x * 2.0f;
        ImGui::PushFont(key_font);
        ellipsis_text(dl, ImVec2(a.x + pad_x, a.y + pad_y), inner_w, text_tertiary, upper(c.label));
        ImGui::PopFont();

        const float value_y = a.y + pad_y + key_font->FontSize + 4.0f;
        float y = value_y;
        bool cut = false;
        std::string full;
        if(wide(c))
        {
            // A bar per group, its length against the largest count, then
            // "7 people" in a column as wide as the widest label; the bars
            // take the rest. Past max_class_lines the rest fold into "N more".
            const auto& cl = *c.classes;
            const size_t shown = cl.size() <= max_class_lines ? cl.size() : max_class_lines - 1;
            const int peak = std::max(1, cl.front().second);
            const float label_gap = 10.0f;
            ImGui::PushFont(key_font);
            float label_w = 0.0f;
            for(size_t k = 0; k < shown; ++k)
                label_w = std::max(label_w, ImGui::CalcTextSize(r_vss::format_detection_count(cl[k].first, cl[k].second).c_str()).x);
            label_w = std::min(label_w, inner_w * 0.5f);
            const float track_w = std::max(20.0f, inner_w - label_w - label_gap);
            const float x0 = a.x + pad_x;
            for(size_t k = 0; k < shown; ++k)
            {
                const float ly = value_y + 2.0f + (float)k * class_line_h;
                const float bar_y = ly + (key_font->FontSize - bar_h) * 0.5f;
                const float len = std::max(3.0f, track_w * (float)cl[k].second / (float)peak);
                dl->AddRectFilled(ImVec2(x0, bar_y), ImVec2(x0 + track_w, bar_y + bar_h), ImGui::GetColorU32(border_subtle));
                dl->AddRectFilled(ImVec2(x0, bar_y), ImVec2(x0 + len, bar_y + bar_h), ImGui::GetColorU32(detection_group_color(cl[k].first)));
                const float tx = x0 + track_w + label_gap;
                const std::string label = r_vss::format_detection_count(cl[k].first, cl[k].second);
                ellipsis_text(dl, ImVec2(tx, ly), a.x + w - pad_x - tx, text_primary, label);
                cut = cut || ImGui::CalcTextSize(label.c_str()).x > a.x + w - pad_x - tx;
            }
            if(shown < cl.size())
            {
                const float ly = value_y + 2.0f + (float)shown * class_line_h;
                ellipsis_text(dl, ImVec2(x0, ly), inner_w, text_tertiary, r_utils::r_string_utils::format("%zu more", cl.size() - shown));
                cut = true;
            }
            ImGui::PopFont();
            if(cut)
                for(const auto& g : cl)
                    full += (full.empty() ? "" : "\n") + r_vss::format_detection_count(g.first, g.second);
        }
        else
        {
            ImGui::PushFont(value_font);
            ellipsis_text(dl, ImVec2(a.x + pad_x, y), inner_w, text_primary, c.value);
            ImGui::PopFont();
            y += value_font->FontSize + 2.0f;

            if(!c.detail.empty())
            {
                ImGui::PushFont(key_font);
                ellipsis_text(dl, ImVec2(a.x + pad_x, y), inner_w, text_tertiary, c.detail);
                ImGui::PopFont();
            }
            y = value_y + value_font->FontSize + detail_h;
            cut = text_w(value_font, c.value) > inner_w || text_w(key_font, c.detail) > inner_w;
            full = c.value;
        }

        if(c.hours && !wide(c))
        {
            // One bar an hour, midnight on the left; empty hours keep a hairline
            // so the day's shape reads.
            const float top = y + 6.0f;
            const float bottom = top + strip_h;
            const float slot = inner_w / 24.0f;
            const int peak = std::max(1, *std::max_element(c.hours->begin(), c.hours->end()));
            for(int h = 0; h < 24; ++h)
            {
                float x0 = a.x + pad_x + (float)h * slot;
                float x1 = x0 + std::max(1.0f, slot - 2.0f);
                int n = (*c.hours)[(size_t)h];
                if(n > 0)
                {
                    float bh = std::max(2.0f, strip_h * (float)n / (float)peak);
                    dl->AddRectFilled(ImVec2(x0, bottom - bh), ImVec2(x1, bottom), ImGui::GetColorU32(accent));
                }
                else
                    dl->AddRectFilled(ImVec2(x0, bottom - 1.0f), ImVec2(x1, bottom), ImGui::GetColorU32(border_default));
            }
        }

        if(ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, b))
        {
            hovered = (int)i;
            hovered_cut = cut;
            hovered_full = full;
        }
    }
    ImGui::Dummy(ImVec2(avail, row_y.back() + row_h.back()));

    // On hover: the hint, after the full value (or every class) and detail if
    // any was cut short. Wrapped, so a long hint stays a readable block in the
    // window.
    if(hovered >= 0)
    {
        const auto& c = stats[hovered];
        if(hovered_cut || !c.hint.empty())
        {
            ImGui::BeginTooltip();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f));
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
            // Not wrapped_text: it wraps at the window's edge, and a tooltip
            // grows to fit, so that wouldn't wrap at all.
            auto tip_text = [](const ImVec4& color, const std::string& t) {
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(t.c_str());
                ImGui::PopStyleColor();
            };
            if(hovered_cut)
            {
                tip_text(text_primary, hovered_full);
                if(!wide(c) && !c.detail.empty())
                    tip_text(text_secondary, c.detail);
                if(!c.hint.empty())
                    ImGui::Dummy(ImVec2(0.0f, 6.0f));
            }
            if(!c.hint.empty())
                tip_text(hovered_cut ? text_secondary : text_primary, c.hint);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleVar();
            ImGui::EndTooltip();
        }
    }
}

inline void stat_cards(const std::vector<std::pair<std::string, std::string>>& stats, const main_window_fonts& fonts)
{
    std::vector<stat_card> cards;
    for(const auto& s : stats)
        cards.push_back({s.first, s.second, std::string(), std::string()});
    stat_cards(cards, fonts);
}

// An on/off switch sized to sit beside a title rather than in a form: the
// label in secondary text at body size, then a small square-cornered track
// whose knob sits right and fills with accent when on. Label and track are
// one click target. Returns true when clicked (and flips *v).
inline float compact_toggle_width(const char* label, const main_window_fonts& fonts)
{
    ImGui::PushFont(font(fonts.key_22));
    float w = ImGui::CalcTextSize(label, nullptr, true).x + 8.0f + 28.0f;
    ImGui::PopFont();
    return w;
}

inline float compact_toggle_height(const main_window_fonts& fonts)
{
    return font(fonts.key_22)->FontSize;
}

inline bool compact_toggle(const char* label, bool* v, const main_window_fonts& fonts)
{
    using namespace r_ui_utils::theme;
    const float track_w = 28.0f;
    const float track_h = 16.0f;
    const float knob_inset = 3.0f;

    ImGui::PushFont(font(fonts.key_22));
    const auto label_size = ImGui::CalcTextSize(label, nullptr, true);
    const float h = std::max(label_size.y, track_h);
    const auto p = ImGui::GetCursorScreenPos();
    const ImVec2 size(label_size.x + 8.0f + track_w, h);

    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##toggle", size);
    ImGui::PopID();
    bool hovered = ImGui::IsItemHovered();
    if(hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if(clicked)
        *v = !*v;

    auto* dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(p.x, p.y + std::floor((h - label_size.y) * 0.5f)), ImGui::GetColorU32(hovered ? text_primary : text_secondary), label, ImGui::FindRenderedTextEnd(label));

    ImVec2 ta(p.x + label_size.x + 8.0f, p.y + std::floor((h - track_h) * 0.5f));
    ImVec2 tb(ta.x + track_w, ta.y + track_h);
    const float knob = track_h - knob_inset * 2.0f;
    if(*v)
    {
        dl->AddRectFilled(ta, tb, ImGui::GetColorU32(hovered ? blue_300 : accent));
        ImVec2 ka(tb.x - knob_inset - knob, ta.y + knob_inset);
        dl->AddRectFilled(ka, ImVec2(ka.x + knob, ka.y + knob), ImGui::GetColorU32(text_on_primary));
    }
    else
    {
        dl->AddRectFilled(ta, tb, ImGui::GetColorU32(surface_sunken));
        dl->AddRect(ta, tb, ImGui::GetColorU32(hovered ? text_secondary : border_default));
        ImVec2 ka(ta.x + knob_inset, ta.y + knob_inset);
        dl->AddRectFilled(ka, ImVec2(ka.x + knob, ka.y + knob), ImGui::GetColorU32(text_tertiary));
    }
    ImGui::PopFont();
    return clicked;
}

// Where the camera's motion and objects come from (its motion tier), and how
// it got there: shown while motion detection is on and the tier is known.
inline r_utils::r_nullable<stat_card> motion_source_card(const sidebar_list_ui_item& item)
{
    r_utils::r_nullable<stat_card> out;
    if(item.motion_detection.is_null() || !item.motion_detection.value() || item.motion_tier < 1 || item.motion_tier > 3)
        return out;

    static const char* values[] = {"", "Revere", "Camera", "Camera + Revere"};
    static const char* meanings[] = {
        "",
        "Revere finds the motion in the video and the objects in it.",
        "The camera's own motion and person, vehicle and animal events.",
        "The camera's own motion events; Revere finds the objects in them."
    };

    std::string detail;
    if(!item.motion_tier_automatic)
        detail = "set by you";
    else if(item.motion_tier > 1)
        detail = "verified";
    else if(item.motion_tier_checked >= 5 && item.motion_tier_matched < item.motion_tier_checked)
        detail = r_utils::r_string_utils::format("camera missed %d of %d", item.motion_tier_checked - item.motion_tier_matched, item.motion_tier_checked);
    else if(item.motion_tier_checked > 0 || item.motion_tier_reason.find("checking") != std::string::npos)
        detail = r_utils::r_string_utils::format("checking \xC2\xB7 %d of 5", std::min(item.motion_tier_checked, 5));

    std::string hint = std::string(meanings[item.motion_tier]);
    if(!item.motion_tier_reason.empty())
    {
        std::string reason = item.motion_tier_reason;
        reason[0] = (char)std::toupper((unsigned char)reason[0]);
        hint += " " + reason + ".";
    }
    if(item.motion_tier_automatic)
        hint += " Change it in Properties.";

    out.set_value(stat_card{"Detection", values[item.motion_tier], detail, hint});
    return out;
}

// The camera pane's stat cards from what's been read so far (nothing until
// the first read lands).
inline std::vector<stat_card> camera_stat_cards(const sidebar_list_ui_item& item, std::chrono::system_clock::time_point now)
{
    std::vector<stat_card> cards;
    auto source = motion_source_card(item);
    if(!item.stats)
    {
        if(!source.is_null())
            cards.push_back(source.value());
        return cards;
    }
    const auto& s = *item.stats;

    if(!s.storage_bytes.is_null())
    {
        std::string v = r_vss::format_storage(s.storage_bytes.value());
        if(!s.kept.is_null())
        {
            auto h = std::chrono::duration_cast<std::chrono::hours>(s.kept.value()).count();
            v += h < 48 ? r_utils::r_string_utils::format(" \xC2\xB7 %lld h", (long long)h)
                        : r_utils::r_string_utils::format(" \xC2\xB7 %lld days", (long long)(h / 24));
        }
        cards.push_back({"Storage used", v, "", "Disk space for this camera's recording, motion and analytics files (allocated up front), and how far back its video goes."});
    }

    const bool motion = !item.motion_detection.is_null() && item.motion_detection.value();
    if(motion)
    {
        cards.push_back({"Motion today", r_utils::r_string_utils::format(s.motion_events_today == 1 ? "%d event" : "%d events", s.motion_events_today), "",
                         "Motion events since midnight, by the hour they started.", &s.motion_by_hour});
    }

    if(s.window_24h.count() > 0)
    {
        double pct = 100.0 * (double)s.recorded_24h.count() / (double)s.window_24h.count();
        std::string up = pct >= 99.95 ? std::string("100%") : r_utils::r_string_utils::format("%.1f%%", pct);
        up += item.reconnects_24h == 1 ? std::string(" \xC2\xB7 1 drop") : r_utils::r_string_utils::format(" \xC2\xB7 %zu drops", item.reconnects_24h);

        std::string hint = s.window_24h < std::chrono::hours(24) ? "Recorded time since recording began (under 24 h ago)" : "Recorded time in the last 24 h";
        hint += r_utils::r_string_utils::format(", and %zu stream %s.", item.reconnects_24h, item.reconnects_24h == 1 ? "drop (lost and reconnected)" : "drops (lost and reconnected)");
        if(now - item.reconnects_counted_since < std::chrono::hours(24))
        {
            time_t t = std::chrono::system_clock::to_time_t(item.reconnects_counted_since);
            char when[32] {};
            std::strftime(when, sizeof(when), "%H:%M", std::localtime(&t));
            hint += std::string(" Drops are counted in memory since revere started (") + when + "), not saved across restarts.";
        }
        else
            hint += " Drops are counted in memory while revere runs.";
        cards.push_back({"Uptime", up, "", hint});
    }

    if(motion)
    {
        cards.push_back({"Last motion",
                         s.last_motion.is_null() ? std::string("None in 24 h") : r_vss::format_ago(std::chrono::duration_cast<std::chrono::seconds>(now - s.last_motion.value())), "",
                         s.last_motion.is_null() ? std::string() : std::string("When the most recent motion event started.")});

        // Beside it: where that motion (and what was in it) came from.
        if(!source.is_null())
            cards.push_back(source.value());

        // Last, as it takes a row of its own once anything's been seen.
        stat_card d {"Detected today", "Nothing yet", "",
                     "Motion events since midnight in which the object detector saw each kind of object, most first."};
        d.classes = &s.detections_today;
        cards.push_back(d);
    }
    return cards;
}

inline void detail_title(const std::string& title, const std::string& subtitle, const main_window_fonts& fonts)
{
    ImGui::PushFont(font(fonts.key_24, true));
    wrapped_text(r_ui_utils::theme::text_primary, title);
    ImGui::PopFont();
    if(!subtitle.empty())
    {
        ImGui::PushFont(font(fonts.key_18));
        wrapped_text(r_ui_utils::theme::text_tertiary, subtitle);
        ImGui::PopFont();
    }
}

// The line under a plugin's title, in the place of a camera's "IP · state":
// a status dot and label, then " · Version 0.0.30" with the full version as
// a tooltip.
inline void plugin_status_line(const plugin_view& p, const main_window_fonts& fonts)
{
    using namespace r_ui_utils::theme;
    std::string version = p.version.empty() ? std::string() : "Version " + short_version(p.version);
    if(!p.has_status && version.empty())
        return;

    ImGui::PushFont(font(fonts.key_18));
    // No item spacing: the " · " carries the gap, as in a camera's subtitle.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
    flow_row row;
    if(p.has_status)
    {
        const float r = 4.0f;
        const float gap = 6.0f;
        row.next(r * 2.0f + gap + ImGui::CalcTextSize(p.status_label.c_str()).x);
        auto pos = ImGui::GetCursorScreenPos();
        float cy = pos.y + ImGui::GetTextLineHeight() * 0.5f;
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + r, cy), r, ImGui::GetColorU32(p.status_color));
        ImGui::Dummy(ImVec2(r * 2.0f + gap, ImGui::GetTextLineHeight()));
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextColored(text_secondary, "%s", p.status_label.c_str());
    }
    if(!version.empty())
    {
        std::string t = p.has_status ? " \xC2\xB7 " + version : version;
        row.next(ImGui::CalcTextSize(t.c_str()).x);
        ImGui::TextColored(text_tertiary, "%s", t.c_str());
        if(ImGui::IsItemHovered() && version != "Version " + p.version)
            ImGui::SetTooltip("%s", p.version.c_str());
    }
    ImGui::PopStyleVar();
    ImGui::PopFont();
}

// The log pane's lines, selectable like text: drag to select (dragging past
// the top or bottom scrolls), double-click for a line, Cmd/Ctrl+C to copy,
// Cmd/Ctrl+A for all of it, or the right-click menu and the Copy all button.
// Only the visible lines are drawn and hit-tested (ImGuiListClipper, one line
// per row), so a full log costs a screenful a frame; the text is gathered
// only when copied. follow: scroll to the newest line.
//
// All of it runs inside log.with_lines(), under log_tail's lock, which the
// logger callback (log_tail::add) also takes: nothing in that lambda may log
// (R_LOG_*), or the UI thread deadlocks on itself. The clipboard and the
// toast run after it, outside the lock.
inline void log_pane(main_window_state& state, const log_tail& log, bool follow)
{
    using r_ui_utils::text_pos;
    auto& io = ImGui::GetIO();
    auto& sel = state.log_selection;
    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 mouse = io.MousePos;
    const ImU32 selected_bg = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
    const float eol_w = ImGui::CalcTextSize(" ").x;
    auto* dl = ImGui::GetWindowDrawList();
    auto text_w = [](const char* b, const char* e) { return ImGui::CalcTextSize(b, e).x; };

    // The pane, not a scrollbar or the button: where a drag starts.
    const bool over_text = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered();
    // ImGui 1.88 (vendored) reports Cmd as io.KeySuper and Ctrl as io.KeyCtrl
    // on every platform, so macOS shortcuts test KeySuper, as its InputText
    // does. Later versions swap the two under ConfigMacOSXBehaviors (Cmd then
    // arrives as KeyCtrl): after an ImGui upgrade this becomes io.KeyCtrl alone.
    const bool mac = io.ConfigMacOSXBehaviors;
    const bool shortcut = (mac ? (io.KeySuper && !io.KeyCtrl) : (io.KeyCtrl && !io.KeySuper)) && !io.KeyAlt && !io.KeyShift;
    const bool keys = ImGui::IsWindowFocused() && shortcut && !io.WantTextInput;

    std::string copy;
    bool copied_all = false;
    log.with_lines([&](const std::deque<std::string>& lines, uint64_t first) {
        const uint64_t last = first + lines.size();
        auto select_all = [&]() {
            if(!lines.empty())
                sel.select({first, 0}, {last - 1, lines.back().size()});
        };
        // The place in the text under p (clamped to the first and last lines).
        auto pos_at = [&](const ImVec2& p) -> text_pos {
            if(lines.empty())
                return {first, 0};
            float row = std::floor((p.y - origin.y) / line_h);
            if(row < 0.0f)
                return {first, 0};
            if(row >= (float)lines.size())
                return {last - 1, lines.back().size()};
            const auto& text = lines[(size_t)row];
            return {first + (uint64_t)row, r_ui_utils::col_at_x(text, p.x - origin.x, text_w)};
        };

        ImGuiListClipper clipper;
        clipper.Begin((int)lines.size(), line_h);
        while(clipper.Step())
        {
            for(int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto& text = lines[i];
                size_t from, to;
                bool eol;
                if(sel.line_span(first + i, text.size(), from, to, eol))
                {
                    auto p = ImGui::GetCursorScreenPos();
                    float x1 = p.x + text_w(text.data(), text.data() + from);
                    float x2 = p.x + text_w(text.data(), text.data() + to) + (eol ? eol_w : 0.0f);
                    dl->AddRectFilled(ImVec2(x1, p.y), ImVec2(x2, p.y + line_h), selected_bg);
                }
                ImGui::TextUnformatted(text.data(), text.data() + text.size());
            }
        }
        if(follow)
            ImGui::SetScrollHereY(1.0f);

        if(over_text)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
            if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                auto p = pos_at(mouse);
                sel.select({p.line, 0}, {p.line, lines.empty() ? 0 : lines[(size_t)(p.line - first)].size()});
                state.log_dragging = false;
            }
            else if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if(io.KeyShift && !sel.empty())
                    sel.extend(pos_at(mouse));
                else
                    sel.begin(pos_at(mouse));
                state.log_dragging = true;
            }
        }
        if(state.log_dragging)
        {
            if(ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                sel.extend(pos_at(mouse));
                if(mouse.y < wp.y)
                    ImGui::SetScrollY(ImGui::GetScrollY() - line_h);
                else if(mouse.y > wp.y + ImGui::GetWindowHeight())
                    ImGui::SetScrollY(ImGui::GetScrollY() + line_h);
            }
            else
                state.log_dragging = false;
        }

        if(keys && ImGui::IsKeyPressed(ImGuiKey_A, false))
            select_all();
        if(keys && ImGui::IsKeyPressed(ImGuiKey_C, false) && !sel.empty())
            copy = r_ui_utils::selected_text(sel, lines, first);

        ImGui::PushStyleColor(ImGuiCol_Text, r_ui_utils::theme::text_primary);
        if(ImGui::BeginPopupContextWindow("##log_menu", ImGuiPopupFlags_MouseButtonRight))
        {
            if(ImGui::MenuItem("Copy", mac ? "Cmd+C" : "Ctrl+C", false, !sel.empty()))
                copy = r_ui_utils::selected_text(sel, lines, first);
            if(ImGui::MenuItem("Select all", mac ? "Cmd+A" : "Ctrl+A", false, !lines.empty()))
                select_all();
            if(ImGui::MenuItem("Copy all", nullptr, false, !lines.empty()))
            {
                copy = r_ui_utils::all_text(lines);
                copied_all = true;
            }
            ImGui::EndPopup();
        }

        // Copy all, pinned to the pane's top right over the text.
        const char* label = "Copy all";
        auto size = button_size(label, true);
        ImGui::SetCursorScreenPos(ImVec2(wp.x + ImGui::GetWindowContentRegionMax().x + ImGui::GetScrollX() - size.x,
                                         wp.y + ImGui::GetWindowContentRegionMin().y + ImGui::GetScrollY()));
        if(styled_button(label, button_kind::normal, true) && !lines.empty())
        {
            copy = r_ui_utils::all_text(lines);
            copied_all = true;
        }
        ImGui::PopStyleColor();
    });

    // Outside the log's lock: the clipboard can take a moment.
    if(!copy.empty())
    {
        ImGui::SetClipboardText(copy.c_str());
        if(copied_all)
            state.show_toast("Log copied to the clipboard.");
    }
}

} // namespace main_window_detail

// Text that acts like a link: link color, hand cursor, underline and the URL
// as a tooltip on hover, open_cb(url) on click. It wraps at the pane's edge
// like other text, and every line of it is part of the link.
template<typename OPEN_CB>
void link_text(const std::string& label, const std::string& url, OPEN_CB open_cb)
{
    const ImVec4 color = r_ui_utils::theme::text_link;
    ImFont* f = ImGui::GetFont();
    const float scale = ImGui::GetFontSize() / f->FontSize;
    const float wrap = std::max(1.0f, ImGui::GetContentRegionAvail().x);

    std::vector<std::pair<const char*, const char*>> lines;
    const char* s = label.c_str();
    const char* end = s + label.size();
    while(s < end)
    {
        const char* e = f->CalcWordWrapPositionA(scale, s, end, wrap);
        if(e <= s)
            e = s + 1;
        lines.push_back({s, e});
        s = e;
        while(s < end && *s == ' ')
            ++s;
    }

    // No item spacing between the lines, so they sit as close as wrapped text does.
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    for(const auto& l : lines)
        ImGui::TextUnformatted(l.first, l.second);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    if(ImGui::IsItemHovered())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        auto origin = ImGui::GetItemRectMin();
        const float line_h = ImGui::GetTextLineHeight();
        auto* dl = ImGui::GetWindowDrawList();
        for(size_t i = 0; i < lines.size(); ++i)
        {
            float w = ImGui::CalcTextSize(lines[i].first, lines[i].second).x;
            float y = origin.y + (float)i * line_h + ImGui::GetTextLineHeight();
            dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + w, y), ImGui::GetColorU32(color));
        }
        ImGui::SetTooltip("%s", url.c_str());
    }
    if(ImGui::IsItemClicked())
        open_cb(url);
}

// The lines a system plugin returns from system_plugin_get_status_message,
// {"lines": [...]}, each one of:
//   {"text"} with an optional "copy" (adds Copy) or "url" (the text links to
//     it, plus Copy URL);
//   {"qr": text}, drawn as a QR code;
//   {"tiles": [{"label", "value", "detail"?, "hint"?}], "title"?}, drawn as
//     stat cards like a camera's, under the title if there is one.
// All strings are display text. Lines and tiles that can't be read, unknown
// line types and unknown fields are skipped, without dropping the rest.
// Text wraps and the buttons wrap onto the next line when they don't fit.
// The buttons are a size between the action row's and SmallButton, so they
// don't tower over the line of text they sit beside.
struct plugin_status_line_view
{
    std::string qr;
    std::string text;
    std::string url;
    std::string copy;
    std::string title;
    std::vector<main_window_detail::stat_card> tiles;
    bool is_tiles {false};
};

// Reads one entry of "lines". False when it's malformed or of a type this
// version doesn't draw.
inline bool parse_plugin_status_line(const nlohmann::json& line, plugin_status_line_view& out)
{
    try
    {
        if(!line.is_object())
            return false;
        auto str = [&](const nlohmann::json& o, const char* key) {
            return o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::string();
        };
        if(line.contains("qr"))
        {
            out.qr = str(line, "qr");
            return !out.qr.empty();
        }
        if(line.contains("tiles"))
        {
            if(!line["tiles"].is_array())
                return false;
            out.is_tiles = true;
            out.title = str(line, "title");
            for(const auto& t : line["tiles"])
            {
                if(!t.is_object())
                    continue;
                main_window_detail::stat_card c {str(t, "label"), str(t, "value"), str(t, "detail"), str(t, "hint")};
                if(!c.label.empty() && !c.value.empty())
                    out.tiles.push_back(std::move(c));
            }
            return !out.tiles.empty();
        }
        out.text = str(line, "text");
        out.url = str(line, "url");
        out.copy = str(line, "copy");
        return !out.text.empty();
    }
    catch(...)
    {
        return false;
    }
}

template<typename OPEN_URL_CB>
void plugin_status_lines(const std::string& status_message, OPEN_URL_CB open_url, const main_window_fonts& fonts)
{
    using namespace main_window_detail;
    if(status_message.empty())
        return;

    nlohmann::json j;
    try { j = nlohmann::json::parse(status_message); }
    catch(...) { return; }
    if(!j.is_object() || !j.contains("lines") || !j["lines"].is_array())
        return;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 4.0f));
    int n = 0;
    for(const auto& json_line : j["lines"])
    {
        plugin_status_line_view line;
        if(!parse_plugin_status_line(json_line, line))
            continue;
        ImGui::PushID(n++);

        if(!line.qr.empty())
        {
            static std::string qr_cached_text;
            static r_utils::r_qr_code::qr_code qr_cached;
            if(line.qr != qr_cached_text)
            {
                qr_cached = r_utils::r_qr_code::encode(line.qr);
                qr_cached_text = line.qr;
            }
            if(qr_cached.size > 0)
            {
                wrapped_text(r_ui_utils::theme::text_tertiary, "Or scan with your phone:");

                // Whole pixels per module so it stays sharp; 4-module quiet zone.
                const int quiet = 4;
                const int span = qr_cached.size + quiet * 2;
                float module = std::floor(ImGui::GetContentRegionAvail().x / span);
                module = std::max(2.0f, std::min(module, 4.0f));
                auto pos = ImGui::GetCursorScreenPos();
                ImVec2 origin(std::floor(pos.x), std::floor(pos.y));
                float side = span * module;

                auto* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + side), IM_COL32(255, 255, 255, 255));
                for(int y = 0; y < qr_cached.size; ++y)
                {
                    for(int x = 0; x < qr_cached.size; ++x)
                    {
                        if(!qr_cached.dark(x, y))
                            continue;
                        ImVec2 a(origin.x + (quiet + x) * module, origin.y + (quiet + y) * module);
                        dl->AddRectFilled(a, ImVec2(a.x + module, a.y + module), IM_COL32(0, 0, 0, 255));
                    }
                }
                ImGui::Dummy(ImVec2(side, side));
            }
        }
        else if(line.is_tiles)
        {
            // A grid is its own block: the same room above it as between a
            // camera's action row and its stats.
            if(n > 1)
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
            if(!line.title.empty())
            {
                ImGui::PushFont(font(fonts.key_18, true));
                wrapped_text(r_ui_utils::theme::text_secondary, line.title);
                ImGui::PopFont();
            }
            stat_cards(line.tiles, fonts);
        }
        else if(!line.url.empty())
        {
            flow_row row;
            row.next(ImGui::CalcTextSize(line.text.c_str()).x);
            ImGui::AlignTextToFramePadding();
            link_text(line.text, line.url, open_url);
            row.after_text();
            if(flow_button(row, "Copy URL"))
                ImGui::SetClipboardText(line.url.c_str());
        }
        else
        {
            flow_row row;
            row.next(ImGui::CalcTextSize(line.text.c_str()).x);
            ImGui::AlignTextToFramePadding();
            wrapped_text(r_ui_utils::theme::text_primary, line.text);
            row.after_text();
            if(!line.copy.empty() && flow_button(row, "Copy"))
                ImGui::SetClipboardText(line.copy.c_str());
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
}

// Adds "Show log" to the View menu that main_menu() draws. ImGui appends to a
// menu submitted again in the same frame, so main_menu() needn't know about it.
inline void main_window_view_menu(main_window_state& state)
{
    push_menu_bar_style();
    if(ImGui::BeginMainMenuBar())
    {
        if(ImGui::BeginMenu("View"))
        {
            ImGui::Separator();
            ImGui::MenuItem("Show log", nullptr, &state.show_log);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    pop_menu_bar_style();
}

inline void main_window(
    uint16_t client_top,
    uint16_t window_width,
    uint16_t window_height,
    main_window_state& state,
    int recording_selected,
    int discovered_selected,
    const std::vector<sidebar_list_ui_item>& recording,
    const std::vector<sidebar_list_ui_item>& discovered,
    const std::vector<plugin_view>& plugins,
    const std::string& status_text,
    bool status_warning,
    const std::string& version_text,
    const log_tail& log,
    const main_window_actions& actions,
    const main_window_fonts& fonts
)
{
    using namespace r_ui_utils::theme;
    using namespace main_window_detail;

    // Which one thing is selected. A plugin selection wins; otherwise the
    // caller's camera selection. With nothing selected, select the first row.
    enum class kind { none, recording, discovered, plugin } sel = kind::none;
    int sel_index = -1;
    if(!state.selected_plugin.empty())
    {
        for(size_t i = 0; i < plugins.size(); ++i)
            if(plugins[i].name == state.selected_plugin) { sel = kind::plugin; sel_index = (int)i; }
        if(sel == kind::none)
            state.selected_plugin.clear();
    }
    if(sel == kind::none && recording_selected >= 0 && recording_selected < (int)recording.size())
    {
        sel = kind::recording;
        sel_index = recording_selected;
    }
    else if(sel == kind::none && discovered_selected >= 0 && discovered_selected < (int)discovered.size())
    {
        sel = kind::discovered;
        sel_index = discovered_selected;
    }
    // With no camera recording yet, nothing is picked for the user, so the
    // detail pane can say how to get started.
    if(sel == kind::none && !recording.empty())
    {
        actions.select_recording(0);
        sel = kind::recording;
        sel_index = 0;
    }

    auto select_plugin = [&](const std::string& name) {
        state.selected_plugin = name;
        actions.select_plugin();
    };

    // Status bar height: one 18-key line plus padding, the same height as the
    // menu bar (push_menu_bar_style), so the two bars read as a pair.
    const float status_h = std::floor(font(fonts.key_18)->FontSize + 18.0f);
    const float area_h = std::max(0.0f, (float)window_height - (float)client_top - status_h);
    const float w = (float)window_width;

    // List width: between 180 and whatever leaves the detail pane 300.
    const float max_list = std::max(180.0f, w - 300.0f);
    state.list_width = std::clamp(state.list_width, 180.0f, max_list);

    const float splitter = 6.0f;
    float top_h = area_h;
    if(state.show_log)
    {
        state.log_height = std::clamp(state.log_height, 80.0f, std::max(80.0f, area_h - 160.0f));
        top_h = std::max(0.0f, area_h - state.log_height - splitter);
    }

    ImGui::SetNextWindowPos(ImVec2(0.0f, (float)client_top));
    ImGui::SetNextWindowSize(ImVec2(w, area_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, surface_page);
    ImGui::Begin("##main_window", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

    auto vertical_rule = [&](const char* id, float h, float& value, float sign) {
        ImGui::InvisibleButton(id, ImVec2(splitter, h));
        bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if(hot)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if(ImGui::IsItemActive())
            value += sign * ImGui::GetIO().MouseDelta.x;
        auto mn = ImGui::GetItemRectMin();
        float x = std::floor(mn.x + splitter * 0.5f);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x, mn.y), ImVec2(x, mn.y + h), ImGui::GetColorU32(hot ? border_strong : border_subtle));
    };

    // ---- List ----
    ImGui::PushStyleColor(ImGuiCol_ChildBg, surface_card);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f));
    ImGui::BeginChild("##list", ImVec2(state.list_width, top_h), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
    {
        section_header("##recording_section", "Recording", recording.size(), state.recording_open, fonts);
        if(state.recording_open && recording.empty())
            empty_state(state.cameras_loaded ? "No cameras recording" : "Loading cameras...", fonts);
        for(int i = 0; state.recording_open && i < (int)recording.size(); ++i)
        {
            const auto& item = recording[i];
            if(list_row("##rec" + std::to_string(i), sel == kind::recording && sel_index == i, camera_dot(item.health), item.label, item.sub_label, fonts))
            {
                state.selected_plugin.clear();
                actions.select_recording(i);
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        section_header("##discovered_section", "Discovered", discovered.size(), state.discovered_open, fonts);
        if(state.discovered_open && discovered.empty())
            empty_state(state.cameras_loaded ? "Nothing discovered yet" : "Loading cameras...", fonts);
        for(int i = 0; state.discovered_open && i < (int)discovered.size(); ++i)
        {
            const auto& item = discovered[i];
            if(list_row("##disc" + std::to_string(i), sel == kind::discovered && sel_index == i, status_neutral_dot, item.label, item.sub_label, fonts))
            {
                state.selected_plugin.clear();
                actions.select_discovered(i);
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        section_header("##plugins_section", "Plugins", plugins.size(), state.plugins_open, fonts);
        if(state.plugins_open && plugins.empty())
            empty_state("No system plugins loaded", fonts);
        for(int i = 0; state.plugins_open && i < (int)plugins.size(); ++i)
        {
            const auto& p = plugins[i];
            ImVec4 dot = p.has_status ? p.status_color : status_neutral_dot;
            std::string right = p.has_status ? p.status_label : (p.enabled ? "Enabled" : "Disabled");
            if(list_row("##plugin" + std::to_string(i), sel == kind::plugin && sel_index == i, dot, plugin_display_name(p), right, fonts))
                select_plugin(p.name);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::SameLine(0.0f, 0.0f);
    vertical_rule("##list_splitter", top_h, state.list_width, 1.0f);
    ImGui::SameLine(0.0f, 0.0f);

    // ---- Detail ----
    ImGui::PushStyleColor(ImGuiCol_ChildBg, surface_page);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 10.0f));
    ImGui::BeginChild("##detail", ImVec2(0.0f, top_h), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
    {
        if(sel == kind::recording)
        {
            const auto& item = recording[sel_index];
            std::string sub = item.sub_label.empty() ? std::string(camera_state(item.health)) : item.sub_label + " \xC2\xB7 " + camera_state(item.health);
            detail_title(item.label, sub, fonts);

            if(item.health == stream_health::failed && !item.retention.empty())
            {
                // The cause, e.g. "recording file missing" (the subtitle says it failed).
                ImGui::PushFont(font(fonts.key_18));
                wrapped_text(status_danger_text, item.retention);
                ImGui::PopFont();
            }

            {
                action_row a;
                if(flow_button(a.row, "View", button_kind::primary))
                    actions.view(sel_index);
                if(ImGui::IsItemHovered())
                    ImGui::SetTooltip("View live video, or scrub back through recordings");
                if(flow_button(a.row, "Properties"))
                    actions.properties(sel_index);
                if(flow_button(a.row, "Open in web UI"))
                    actions.open_url(local_web_ui_camera_url(item.camera_id));
                if(!item.action_label.empty() && flow_button(a.row, item.action_label.c_str()))
                    actions.repair(sel_index);
                if(flow_button(a.row, "Remove", button_kind::ghost_destructive, false, true))
                    actions.remove(sel_index);
            }

            std::vector<stat_card> stats;
            if(item.health != stream_health::failed)
            {
                if(!item.kbps.empty()) stats.push_back({"Bitrate", item.kbps, {}, {}});
                if(!item.retention.empty()) stats.push_back({"Retention", item.retention, {}, {}});
            }
            if(!item.resolution.empty()) stats.push_back({"Resolution", item.resolution, {}, {}});
            if(!item.frame_rate.empty()) stats.push_back({"Frame rate", item.frame_rate, {}, {}});
            if(!item.codec.empty()) stats.push_back({"Codec", upper(item.codec), {}, {}});
            for(auto& c : camera_stat_cards(item, std::chrono::system_clock::now()))
                stats.push_back(std::move(c));
            stat_cards(stats, fonts);

            if(!item.motion_detection.is_null())
            {
                ImGui::PushFont(font(fonts.key_18));
                wrapped_text(text_tertiary, item.motion_detection.value() ? "Motion detection on" : "Motion detection off");
                ImGui::PopFont();
            }
        }
        else if(sel == kind::discovered)
        {
            const auto& item = discovered[sel_index];
            detail_title(item.label, item.sub_label, fonts);
            action_row a;
            if(flow_button(a.row, "Record", button_kind::primary))
                actions.record(sel_index);
            if(flow_button(a.row, "Forget", button_kind::ghost))
                actions.forget(sel_index);
        }
        else if(sel == kind::plugin)
        {
            const auto& p = plugins[sel_index];
            // Enabled sits at the right of the title row when both fit,
            // otherwise on its own line under the status line.
            const std::string title = p.display_name.empty() ? p.name : p.display_name;
            const char* toggle_label = "Enabled";
            const float toggle_w = compact_toggle_width(toggle_label, fonts);
            const float avail = ImGui::GetContentRegionAvail().x;
            const auto row = ImGui::GetCursorScreenPos();
            ImGui::PushFont(font(fonts.key_24, true));
            const float title_w = ImGui::CalcTextSize(title.c_str()).x;
            const float title_h = ImGui::GetTextLineHeight();
            ImGui::PopFont();
            const bool toggle_in_title_row = title_w + 16.0f + toggle_w <= avail;

            bool enabled = p.enabled;
            detail_title(title, std::string(), fonts);
            if(toggle_in_title_row)
            {
                auto below = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(row.x + avail - toggle_w, row.y + std::floor((title_h - compact_toggle_height(fonts)) * 0.5f)));
                if(compact_toggle(toggle_label, &enabled, fonts))
                    actions.set_plugin_enabled(p.name, enabled);
                ImGui::SetCursorScreenPos(below);
            }
            plugin_status_line(p, fonts);
            if(!toggle_in_title_row && compact_toggle(toggle_label, &enabled, fonts))
                actions.set_plugin_enabled(p.name, enabled);

            // The same gap above the content as above a camera's action row.
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::PushFont(font(fonts.key_22));
            plugin_status_lines(p.status_message, actions.open_url, fonts);
            ImGui::PopFont();
        }
        else if(!state.cameras_loaded)
        {
            // Until the camera list has loaded, rather than "No cameras yet".
            detail_title("Loading cameras...", std::string(), fonts);
        }
        else
        {
            detail_title(recording.empty() ? "No cameras yet" : "Nothing selected", std::string(), fonts);
            ImGui::PushFont(font(fonts.key_22));
            if(recording.empty())
            {
                wrapped_text(text_secondary, "Cameras Revere finds on your network appear under Discovered on the left. Select one and choose Record to start recording it.");
                wrapped_text(text_secondary, "A camera that isn't found can be added by its RTSP address.");
            }
            else wrapped_text(text_secondary, "Select a camera or plugin on the left.");
            ImGui::PopFont();
            if(recording.empty() && actions.add_rtsp_camera)
            {
                action_row a;
                if(flow_button(a.row, "Add RTSP camera..."))
                    actions.add_rtsp_camera();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // ---- Log ----
    if(state.show_log)
    {
        ImGui::InvisibleButton("##log_splitter", ImVec2(w, splitter));
        bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if(hot)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if(ImGui::IsItemActive())
            state.log_height -= ImGui::GetIO().MouseDelta.y;
        auto mn = ImGui::GetItemRectMin();
        float y = std::floor(mn.y + splitter * 0.5f);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, y), ImVec2(mn.x + w, y), ImGui::GetColorU32(hot ? border_strong : border_subtle));

        ImGui::PushStyleColor(ImGuiCol_ChildBg, surface_sunken);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f));
        ImGui::BeginChild("##log", ImVec2(w, 0.0f), false, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushFont(font(fonts.key_18));
        ImGui::PushStyleColor(ImGuiCol_Text, text_secondary);
        // Follow new lines while scrolled to the bottom; stay put after
        // scrolling up, or while selecting.
        const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
        log_pane(state, log, at_bottom && !state.log_dragging);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    // ---- Status bar ----
    ImGui::SetNextWindowPos(ImVec2(0.0f, (float)window_height - status_h));
    ImGui::SetNextWindowSize(ImVec2(w, status_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
    ImGui::Begin("##status_bar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(font(fonts.key_18));
    {
        auto* dl = ImGui::GetWindowDrawList();
        auto wp = ImGui::GetWindowPos();
        dl->AddLine(wp, ImVec2(wp.x + w, wp.y), ImGui::GetColorU32(border_subtle));

        const float line_h = ImGui::GetFontSize();
        const float cy = wp.y + status_h * 0.5f;
        // The menu bar's first title starts about 18px in; match it.
        const float left = wp.x + 18.0f;
        const float right = wp.x + w - 18.0f;
        const float gap = 12.0f;

        // Version on the right, when there's room for it next to the status text.
        const float version_w = version_text.empty() ? 0.0f : ImGui::CalcTextSize(version_text.c_str()).x;
        const float status_w = ImGui::CalcTextSize(status_text.c_str()).x;

        // Chips: "name: label", or just "label", or just the dot, whichever fits.
        const float r = 4.0f;
        const float chip_pad = 8.0f;
        struct chip { std::string full; std::string text; float width; };
        std::vector<chip> chips;
        for(const auto& p : plugins)
        {
            std::string label = p.has_status ? p.status_label : (p.enabled ? "Enabled" : "Disabled");
            chips.push_back({plugin_display_name(p) + ": " + label, label, 0.0f});
        }
        auto chips_width = [&](int mode) {
            float total = 0.0f;
            for(auto& c : chips)
            {
                const std::string& t = mode == 0 ? c.full : c.text;
                float tw = mode == 2 ? 0.0f : ImGui::CalcTextSize(t.c_str()).x + 6.0f;
                total += chip_pad + r * 2.0f + tw + chip_pad + gap;
            }
            return total;
        };
        const float min_status = std::min(status_w, 140.0f);
        const float room = (right - left) - min_status - gap;
        int mode = 0;
        while(mode < 2 && chips_width(mode) > room)
            ++mode;
        const float chips_w = chips.empty() ? 0.0f : chips_width(mode);
        const bool show_version = version_w > 0.0f && left + status_w + gap + chips_w + version_w <= right;

        // Status text, clipped with "..." (full text in a tooltip) only when the bar can't hold it.
        float status_room = (right - left) - chips_w - (show_version ? version_w + gap : 0.0f);
        status_room = std::max(min_status, std::min(status_w, status_room));
        ImGui::SetCursorScreenPos(ImVec2(left, cy - line_h * 0.5f));
        ImGui::InvisibleButton("##status_text", ImVec2(std::max(1.0f, status_room), line_h));
        ellipsis_text(dl, ImVec2(left, cy - line_h * 0.5f), status_room, status_warning ? status_warning_text : text_secondary, status_text);
        if(status_room < status_w && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", status_text.c_str());

        float x = left + status_room + gap;
        for(size_t i = 0; i < chips.size(); ++i)
        {
            const auto& p = plugins[i];
            const std::string t = mode == 0 ? chips[i].full : (mode == 1 ? chips[i].text : std::string());
            float tw = t.empty() ? 0.0f : ImGui::CalcTextSize(t.c_str()).x + 6.0f;
            float cw = chip_pad + r * 2.0f + tw + chip_pad;
            float ch = line_h + 4.0f;
            if(x + cw > right)
                break;
            ImVec2 a(x, cy - ch * 0.5f);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID((int)i);
            bool clicked = ImGui::InvisibleButton("##chip", ImVec2(cw, ch));
            bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            ImVec2 b(a.x + cw, a.y + ch);
            // No box at rest; on hover, a background and a hand cursor say it
            // opens the plugin's pane.
            if(hovered)
            {
                dl->AddRectFilled(a, b, ImGui::GetColorU32(surface_raised));
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if(mode != 0)
                    ImGui::SetTooltip("%s", chips[i].full.c_str());
            }
            dl->AddCircleFilled(ImVec2(a.x + chip_pad + r, cy), r, ImGui::GetColorU32(p.has_status ? p.status_color : status_neutral_dot));
            if(!t.empty())
                dl->AddText(ImVec2(a.x + chip_pad + r * 2.0f + 6.0f, cy - line_h * 0.5f), ImGui::GetColorU32(text_secondary), t.c_str());
            if(clicked)
                select_plugin(p.name);
            x += cw + gap;
        }

        if(show_version)
            dl->AddText(ImVec2(right - version_w, cy - line_h * 0.5f), ImGui::GetColorU32(text_tertiary), version_text.c_str());
    }
    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // ---- Toast ----
    const double now = ImGui::GetTime();
    if(!state.toast_text.empty() && now < state.toast_until)
    {
        const float toast_w = std::min(360.0f, w - 40.0f);
        const float fade = (float)std::clamp((state.toast_until - now) / 0.6, 0.0, 1.0);
        ImGui::SetNextWindowPos(ImVec2(w - 20.0f, (float)window_height - status_h - 16.0f), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(toast_w, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, surface_raised);
        ImGui::PushStyleColor(ImGuiCol_Border, border_default);
        ImGui::Begin("##toast", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::PushFont(font(fonts.key_22));
        ImGui::PushTextWrapPos(toast_w - 28.0f);
        ImGui::TextColored(text_primary, "%s", state.toast_text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        {
            flow_row row;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 4.0f));
            if(!state.toast_id.empty() && flow_button(row, "Don't show again", button_kind::ghost))
            {
                if(actions.dismiss_notice)
                    actions.dismiss_notice(state.toast_id);
                state.toast_until = 0.0;
            }
            if(flow_button(row, "OK", button_kind::ghost))
                state.toast_until = 0.0;
            ImGui::PopStyleVar();
        }
        // Hovering keeps it up, so it doesn't fade out under the pointer.
        if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            state.toast_until = std::max(state.toast_until, now + 1.5);
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }
}

}

#endif
