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
#include "r_utils/r_qr_code.h"
#include "r_utils/3rdparty/json/json.h"
#include <algorithm>
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

// The last lines the logger wrote, for the log pane. add() may be called from
// any thread (it's installed as the logger callback); the UI reads under the lock.
class log_tail
{
public:
    explicit log_tail(size_t max_lines = 1000) : _max_lines(max_lines) {}

    void add(const std::string& line)
    {
        std::lock_guard<std::mutex> g(_mtx);
        _lines.push_back(line);
        while(_lines.size() > _max_lines)
            _lines.pop_front();
    }

    template<typename CB>
    void with_lines(CB cb) const
    {
        std::lock_guard<std::mutex> g(_mtx);
        cb(_lines);
    }

private:
    mutable std::mutex _mtx;
    std::deque<std::string> _lines;
    size_t _max_lines;
};

struct main_window_state
{
    float list_width {260.0f};
    // Which list sections are open. In memory for now; to be saved with the
    // window placement, like list_width.
    bool recording_open {true};
    bool discovered_open {true};
    bool plugins_open {true};
    bool show_log {false};
    float log_height {180.0f};
    std::string selected_plugin;    // empty unless a plugin row is selected
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
};

struct main_window_fonts
{
    std::string key_24;
    std::string key_22;
    std::string key_18;
};

// "<name>_system_plugin" reads as "<name>" in the list and the status bar.
inline std::string plugin_short_name(const std::string& name)
{
    const std::string suffix = "_system_plugin";
    if(name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        return name.substr(0, name.size() - suffix.size());
    return name;
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

// Small labelled values in a grid that fits as many columns of at least
// min_width as the pane allows.
// One tile of a stat_cards grid: a small uppercase label over a value, with an
// optional detail line under the value and an optional hover tooltip.
struct stat_card
{
    std::string label;
    std::string value;
    std::string detail;
    std::string hint;
};

inline void stat_cards(const std::vector<stat_card>& stats, const main_window_fonts& fonts)
{
    using namespace r_ui_utils::theme;
    if(stats.empty())
        return;

    const float gap = 10.0f;
    const float pad_x = 10.0f;
    const float pad_y = 8.0f;
    ImFont* key_font = font(fonts.key_18);
    ImFont* value_font = font(fonts.key_22, true);
    auto text_w = [](ImFont* f, const std::string& t) {
        return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, t.c_str()).x;
    };
    // A value too long for its card ends in an ellipsis and shows in full on hover.
    const float min_width = 130.0f;
    // Every card in the grid is as tall as the tallest, so rows line up.
    bool any_detail = std::any_of(stats.begin(), stats.end(), [](const stat_card& c) { return !c.detail.empty(); });
    const float detail_h = any_detail ? 2.0f + key_font->FontSize : 0.0f;
    const float card_h = pad_y + key_font->FontSize + 4.0f + value_font->FontSize + detail_h + pad_y;

    const float avail = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)((avail + gap) / (min_width + gap)));
    cols = std::min(cols, (int)stats.size());
    const float card_w = (avail - gap * (float)(cols - 1)) / (float)cols;
    const int rows = ((int)stats.size() + cols - 1) / cols;

    auto origin = ImGui::GetCursorScreenPos();
    auto* dl = ImGui::GetWindowDrawList();
    int hovered = -1;
    for(size_t i = 0; i < stats.size(); ++i)
    {
        int col = (int)i % cols;
        int row = (int)i / cols;
        ImVec2 a(origin.x + (float)col * (card_w + gap), origin.y + (float)row * (card_h + gap));
        ImVec2 b(a.x + card_w, a.y + card_h);
        dl->AddRectFilled(a, b, ImGui::GetColorU32(surface_card));
        dl->AddRect(a, b, ImGui::GetColorU32(border_subtle));

        float y = a.y + pad_y;
        ImGui::PushFont(key_font);
        ellipsis_text(dl, ImVec2(a.x + pad_x, y), card_w - pad_x * 2.0f, text_tertiary, upper(stats[i].label));
        ImGui::PopFont();
        y += key_font->FontSize + 4.0f;

        ImGui::PushFont(value_font);
        ellipsis_text(dl, ImVec2(a.x + pad_x, y), card_w - pad_x * 2.0f, text_primary, stats[i].value);
        ImGui::PopFont();
        y += value_font->FontSize + 2.0f;

        if(!stats[i].detail.empty())
        {
            ImGui::PushFont(key_font);
            ellipsis_text(dl, ImVec2(a.x + pad_x, y), card_w - pad_x * 2.0f, text_tertiary, stats[i].detail);
            ImGui::PopFont();
        }

        if(ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, b))
            hovered = (int)i;
    }
    ImGui::Dummy(ImVec2(avail, (float)rows * card_h + (float)(rows - 1) * gap));

    // On hover: the hint, after the full value and detail if either was cut
    // short. Wrapped, so a long hint stays a readable block in the window.
    if(hovered >= 0)
    {
        const auto& c = stats[hovered];
        const float inner = card_w - pad_x * 2.0f;
        const bool cut = text_w(value_font, c.value) > inner || text_w(key_font, c.detail) > inner;
        if(cut || !c.hint.empty())
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
            if(cut)
            {
                tip_text(text_primary, c.value);
                if(!c.detail.empty())
                    tip_text(text_secondary, c.detail);
                if(!c.hint.empty())
                    ImGui::Dummy(ImVec2(0.0f, 6.0f));
            }
            if(!c.hint.empty())
                tip_text(cut ? text_secondary : text_primary, c.hint);
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
    if(sel == kind::none)
    {
        if(!recording.empty()) { actions.select_recording(0); sel = kind::recording; sel_index = 0; }
        else if(!discovered.empty()) { actions.select_discovered(0); sel = kind::discovered; sel_index = 0; }
        else if(!plugins.empty()) { state.selected_plugin = plugins[0].name; sel = kind::plugin; sel_index = 0; }
    }

    auto select_plugin = [&](const std::string& name) {
        state.selected_plugin = name;
        actions.select_plugin();
    };

    // Status bar height: one 18-key line plus padding.
    const float status_h = std::floor(font(fonts.key_18)->FontSize + 12.0f);
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
            empty_state("No cameras recording", fonts);
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
            empty_state("Nothing discovered yet", fonts);
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
                if(flow_button(a.row, "View live", button_kind::primary))
                    actions.view(sel_index);
                if(flow_button(a.row, "Properties"))
                    actions.properties(sel_index);
                if(flow_button(a.row, "Open in web UI"))
                    actions.open_url(local_web_ui_camera_url(item.camera_id));
                if(!item.action_label.empty() && flow_button(a.row, item.action_label.c_str()))
                    actions.repair(sel_index);
                if(flow_button(a.row, "Remove", button_kind::ghost_destructive, false, true))
                    actions.remove(sel_index);
            }

            std::vector<std::pair<std::string, std::string>> stats;
            if(item.health != stream_health::failed)
            {
                if(!item.kbps.empty()) stats.push_back({"Bitrate", item.kbps});
                if(!item.retention.empty()) stats.push_back({"Retention", item.retention});
            }
            if(!item.resolution.empty()) stats.push_back({"Resolution", item.resolution});
            if(!item.frame_rate.empty()) stats.push_back({"Frame rate", item.frame_rate});
            if(!item.codec.empty()) stats.push_back({"Codec", upper(item.codec)});
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
        else
        {
            ImGui::PushFont(font(fonts.key_18));
            wrapped_text(text_tertiary, "Cameras found on your network, cameras you record and system plugins appear on the left.");
            ImGui::PopFont();
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
        // Follow new lines while scrolled to the bottom; stay put after scrolling up.
        const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
        log.with_lines([&](const std::deque<std::string>& lines) {
            ImGuiListClipper clipper;
            clipper.Begin((int)lines.size());
            while(clipper.Step())
                for(int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    ImGui::TextUnformatted(lines[i].c_str());
        });
        if(at_bottom)
            ImGui::SetScrollHereY(1.0f);
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, surface_page);
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
        const float left = wp.x + 10.0f;
        const float right = wp.x + w - 10.0f;
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
            if(hovered)
            {
                dl->AddRectFilled(a, b, ImGui::GetColorU32(surface_raised));
                ImGui::SetTooltip("%s", chips[i].full.c_str());
            }
            dl->AddRect(a, b, ImGui::GetColorU32(border_default));
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
}

}

#endif
