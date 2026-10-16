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
    std::string version;        // empty when the plugin doesn't report one
    bool enabled {false};
    bool has_status {false};    // false when the plugin has no status export
    std::string status_label;
    ImVec4 status_color {r_ui_utils::theme::status_neutral_dot};
    std::string status_message; // JSON from system_plugin_get_status_message, may be empty
};

// Fills the status fields of a plugin_view from system_plugin_get_status's JSON:
// {"label": "...", "color": [r, g, b]}. Leaves has_status false if it can't be read.
inline void parse_plugin_status(const std::string& status_json, plugin_view& pv)
{
    if(status_json.empty())
        return;
    try
    {
        auto sj = nlohmann::json::parse(status_json);
        pv.status_label = sj.value("label", std::string("Unknown"));
        auto c = sj.value("color", std::vector<int>{100, 100, 100});
        auto channel = [&](size_t i) { return (float)(c.size() > i ? std::clamp(c[i], 0, 255) : 100) / 255.0f; };
        pv.status_color = ImVec4(channel(0), channel(1), channel(2), 1.0f);
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

// "revere_cloud_system_plugin" reads as "revere_cloud" in the list and the status bar.
inline std::string plugin_short_name(const std::string& name)
{
    const std::string suffix = "_system_plugin";
    if(name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        return name.substr(0, name.size() - suffix.size());
    return name;
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

    void next(float item_width)
    {
        if(!first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + item_width <= right)
            ImGui::SameLine();
        first = false;
    }
};

enum class button_kind { normal, primary, ghost, ghost_destructive };

inline ImVec2 button_size(const char* label, bool small = false)
{
    auto t = ImGui::CalcTextSize(label, nullptr, true);
    auto pad = ImGui::GetStyle().FramePadding;
    return small ? ImVec2(t.x + pad.x * 2.0f, t.y) : ImVec2(t.x + pad.x * 2.0f, t.y + pad.y * 2.0f);
}

inline bool flow_button(flow_row& row, const char* label, button_kind kind = button_kind::normal, bool small = false)
{
    using namespace r_ui_utils::theme;
    row.next(button_size(label, small).x);

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
        default:
            break;
    }
    bool clicked = small ? ImGui::SmallButton(label) : ImGui::Button(label);
    ImGui::PopStyleColor(pushed);
    return clicked;
}

// A status dot followed by a label, as one item of a flow row.
inline void flow_status(flow_row& row, const ImVec4& dot, const ImVec4& text_color, const std::string& label)
{
    const float r = 4.0f;
    const float gap = 6.0f;
    auto t = ImGui::CalcTextSize(label.c_str());
    row.next(r * 2.0f + gap + t.x);

    // Line the dot up with the text of a framed widget on the same line.
    ImGui::AlignTextToFramePadding();
    auto p = ImGui::GetCursorScreenPos();
    float cy = p.y + ImGui::GetFrameHeight() * 0.5f;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, cy), r, ImGui::GetColorU32(dot));
    ImGui::Dummy(ImVec2(r * 2.0f + gap, ImGui::GetFrameHeight()));
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(text_color, "%s", label.c_str());
}

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

inline void section_header(const std::string& title, size_t count, const main_window_fonts& fonts)
{
    ImGui::PushFont(font(fonts.key_18));
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    ImGui::TextColored(r_ui_utils::theme::text_tertiary, "%s \xC2\xB7 %zu", upper(title).c_str(), count);
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
inline void stat_cards(const std::vector<std::pair<std::string, std::string>>& stats, const main_window_fonts& fonts)
{
    using namespace r_ui_utils::theme;
    if(stats.empty())
        return;

    const float min_width = 130.0f;
    const float gap = 10.0f;
    const float pad_x = 10.0f;
    const float pad_y = 8.0f;
    ImFont* key_font = font(fonts.key_18);
    ImFont* value_font = font(fonts.key_22, true);
    const float card_h = pad_y + key_font->FontSize + 4.0f + value_font->FontSize + pad_y;

    const float avail = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)((avail + gap) / (min_width + gap)));
    cols = std::min(cols, (int)stats.size());
    const float card_w = (avail - gap * (float)(cols - 1)) / (float)cols;
    const int rows = ((int)stats.size() + cols - 1) / cols;

    auto origin = ImGui::GetCursorScreenPos();
    auto* dl = ImGui::GetWindowDrawList();
    for(size_t i = 0; i < stats.size(); ++i)
    {
        int col = (int)i % cols;
        int row = (int)i / cols;
        ImVec2 a(origin.x + (float)col * (card_w + gap), origin.y + (float)row * (card_h + gap));
        ImVec2 b(a.x + card_w, a.y + card_h);
        dl->AddRectFilled(a, b, ImGui::GetColorU32(surface_card));
        dl->AddRect(a, b, ImGui::GetColorU32(border_subtle));

        ImGui::PushFont(key_font);
        ellipsis_text(dl, ImVec2(a.x + pad_x, a.y + pad_y), card_w - pad_x * 2.0f, text_tertiary, upper(stats[i].first));
        ImGui::PopFont();

        ImGui::PushFont(value_font);
        ellipsis_text(dl, ImVec2(a.x + pad_x, a.y + pad_y + key_font->FontSize + 4.0f), card_w - pad_x * 2.0f, text_primary, stats[i].second);
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(avail, (float)rows * card_h + (float)(rows - 1) * gap));
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

} // namespace main_window_detail

// The lines a system plugin returns from system_plugin_get_status_message,
// {"lines": [...]}: each {"text"} with an optional "copy" (adds Copy) or "url"
// (adds Open and Copy URL), or {"qr": text} drawn as a QR code. Text wraps and
// the buttons wrap onto the next line when they don't fit.
template<typename OPEN_URL_CB>
void plugin_status_lines(const std::string& status_message, OPEN_URL_CB open_url)
{
    using namespace main_window_detail;
    if(status_message.empty())
        return;

    nlohmann::json j;
    try { j = nlohmann::json::parse(status_message); }
    catch(...) { return; }
    if(!j.contains("lines") || !j["lines"].is_array())
        return;

    int n = 0;
    for(const auto& line : j["lines"])
    {
        if(!line.is_object())
            continue;
        ImGui::PushID(n++);

        if(line.contains("qr") && line["qr"].is_string())
        {
            auto qr_text = line["qr"].get<std::string>();
            static std::string qr_cached_text;
            static r_utils::r_qr_code::qr_code qr_cached;
            if(qr_text != qr_cached_text)
            {
                qr_cached = r_utils::r_qr_code::encode(qr_text);
                qr_cached_text = qr_text;
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
            ImGui::PopID();
            continue;
        }

        auto text = line.value("text", std::string());
        if(text.empty())
        {
            ImGui::PopID();
            continue;
        }

        if(line.contains("url") && line["url"].is_string())
        {
            auto url = line["url"].get<std::string>();
            flow_row row;
            row.next(ImGui::CalcTextSize(text.c_str()).x);
            wrapped_text(r_ui_utils::theme::text_tertiary, text);
            row.after_text();
            if(flow_button(row, "Open", button_kind::normal, true))
                open_url(url);
            if(flow_button(row, "Copy URL", button_kind::normal, true))
                ImGui::SetClipboardText(url.c_str());
        }
        else
        {
            flow_row row;
            row.next(ImGui::CalcTextSize(text.c_str()).x);
            wrapped_text(r_ui_utils::theme::text_primary, text);
            row.after_text();
            if(line.contains("copy") && line["copy"].is_string())
            {
                auto copy = line["copy"].get<std::string>();
                if(flow_button(row, "Copy", button_kind::normal, true))
                    ImGui::SetClipboardText(copy.c_str());
            }
        }
        ImGui::PopID();
    }
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
        section_header("Recording", recording.size(), fonts);
        if(recording.empty())
            empty_state("No cameras recording", fonts);
        for(int i = 0; i < (int)recording.size(); ++i)
        {
            const auto& item = recording[i];
            if(list_row("##rec" + std::to_string(i), sel == kind::recording && sel_index == i, camera_dot(item.health), item.label, item.sub_label, fonts))
            {
                state.selected_plugin.clear();
                actions.select_recording(i);
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        section_header("Discovered", discovered.size(), fonts);
        if(discovered.empty())
            empty_state("Nothing discovered yet", fonts);
        for(int i = 0; i < (int)discovered.size(); ++i)
        {
            const auto& item = discovered[i];
            if(list_row("##disc" + std::to_string(i), sel == kind::discovered && sel_index == i, status_neutral_dot, item.label, item.sub_label, fonts))
            {
                state.selected_plugin.clear();
                actions.select_discovered(i);
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        section_header("Plugins", plugins.size(), fonts);
        if(plugins.empty())
            empty_state("No system plugins loaded", fonts);
        for(int i = 0; i < (int)plugins.size(); ++i)
        {
            const auto& p = plugins[i];
            ImVec4 dot = p.has_status ? p.status_color : status_neutral_dot;
            std::string right = p.has_status ? p.status_label : (p.enabled ? "Enabled" : "Disabled");
            if(list_row("##plugin" + std::to_string(i), sel == kind::plugin && sel_index == i, dot, plugin_short_name(p.name), right, fonts))
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
                flow_row row;
                if(flow_button(row, "View live", button_kind::primary))
                    actions.view(sel_index);
                if(flow_button(row, "Properties"))
                    actions.properties(sel_index);
                if(!item.action_label.empty() && flow_button(row, item.action_label.c_str()))
                    actions.repair(sel_index);
                if(flow_button(row, "Remove", button_kind::ghost_destructive))
                    actions.remove(sel_index);
            }

            std::vector<std::pair<std::string, std::string>> stats;
            if(item.health != stream_health::failed)
            {
                if(!item.kbps.empty()) stats.push_back({"Bitrate", item.kbps});
                if(!item.retention.empty()) stats.push_back({"Retention", item.retention});
            }
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
            flow_row row;
            if(flow_button(row, "Record", button_kind::primary))
                actions.record(sel_index);
            if(flow_button(row, "Forget", button_kind::ghost))
                actions.forget(sel_index);
        }
        else if(sel == kind::plugin)
        {
            const auto& p = plugins[sel_index];
            detail_title(p.name, p.version.empty() ? std::string() : "version " + p.version, fonts);

            {
                flow_row row;
                bool enabled = p.enabled;
                const char* label = "Enabled";
                row.next(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label).x);
                if(ImGui::Checkbox(label, &enabled))
                    actions.set_plugin_enabled(p.name, enabled);
                if(p.has_status)
                    flow_status(row, p.status_color, text_secondary, p.status_label);
            }

            ImGui::PushFont(font(fonts.key_22));
            plugin_status_lines(p.status_message, actions.open_url);
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
            chips.push_back({plugin_short_name(p.name) + ": " + label, label, 0.0f});
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
