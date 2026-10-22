
#ifndef __revere_dialogs_h
#define __revere_dialogs_h

#include "main_window.h"
#include "rtsp_source_camera_config.h"
#include <initializer_list>

// The desktop app's dialogs, on one frame so they share a look: a fixed
// content width, labels above full-width fields, secondary text for
// explanations, errors only after the user tries to go on, and an action row
// with the main action (primary) at the right. Enter takes the main action
// and Esc cancels.

namespace revere
{

using main_window_detail::button_kind;
using main_window_detail::button_size;
using main_window_detail::styled_button;

// The main window's font keys, for dialogs drawn from wizard steps (which are
// set up before the fonts load). main() sets it once the fonts are loaded.
inline main_window_fonts g_dialog_fonts;

class dialog_frame
{
public:
    struct button
    {
        const char* label;
        button_kind kind {button_kind::normal};
        bool enabled {true};
    };

    // No action from the action row.
    static constexpr int none = -1;
    // The action row's left button.
    static constexpr int left = -2;

    explicit dialog_frame(const char* popup_name, float width = 440.0f, ImGuiWindowFlags extra_flags = 0) :
        _width(width)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 16.0f));
        _open = begin_modal(popup_name, ImGuiWindowFlags_AlwaysAutoResize | extra_flags);
        ImGui::PopStyleVar();
        if(!_open)
            return;
        _appearing = ImGui::IsWindowAppearing();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
        ImGui::Dummy(ImVec2(_width, 0.0f));
    }

    ~dialog_frame()
    {
        if(!_open)
            return;
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }

    dialog_frame(const dialog_frame&) = delete;
    dialog_frame& operator=(const dialog_frame&) = delete;

    explicit operator bool() const { return _open; }
    bool appearing() const { return _appearing; }
    float width() const { return _width; }

    // A heading inside the dialog, e.g. the camera's name.
    void heading(const std::string& text)
    {
        ImGui::PushFont(main_window_detail::font(g_dialog_fonts.key_24, true));
        wrapped(r_ui_utils::theme::text_primary, text);
        ImGui::PopFont();
        gap(2.0f);
    }

    // Explanatory text, wrapped to the dialog's width.
    void body(const std::string& text, const ImVec4& color = r_ui_utils::theme::text_secondary)
    {
        wrapped(color, text);
    }

    // A wizard step's heading, carried in its popup name after "##" (see
    // assignment_state::step_title()); nothing if there isn't one.
    void step_heading(const std::string& popup_name)
    {
        auto pos = popup_name.find("##");
        if(pos != std::string::npos && pos + 2 < popup_name.size())
            heading(popup_name.substr(pos + 2));
    }

    void label(const char* text) { ImGui::TextUnformatted(text); }

    // A text field, the dialog's full width unless width is given. Enter in it
    // counts as the main action (see enter()).
    bool input(const char* id, char* buf, size_t size, ImGuiInputTextFlags flags = 0, float width = -1.0f, const char* hint = nullptr, bool focus = false)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
        if(focus && _appearing)
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(width > 0.0f ? width : _width);
        bool entered = hint ?
            ImGui::InputTextWithHint(id, hint, buf, size, flags | ImGuiInputTextFlags_EnterReturnsTrue) :
            ImGui::InputText(id, buf, size, flags | ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleVar();
        _enter |= entered;
        return entered;
    }

    // A labelled field: the label above, the field under it.
    bool field(const char* label_text, const char* id, char* buf, size_t size, ImGuiInputTextFlags flags = 0, const char* hint = nullptr, bool focus = false)
    {
        gap(2.0f);
        label(label_text);
        return input(id, buf, size, flags, -1.0f, hint, focus);
    }

    void error(const std::string& text) { wrapped(r_ui_utils::theme::status_danger_text, text); }

    // A small uppercase section title with a rule under it.
    void section(const char* title)
    {
        gap(8.0f);
        ImGui::PushFont(main_window_detail::font(g_dialog_fonts.key_18, true));
        ImGui::TextColored(r_ui_utils::theme::text_tertiary, "%s", main_window_detail::upper(title).c_str());
        ImGui::PopFont();
        ImGui::Separator();
    }

    void gap(float h) { ImGui::Dummy(ImVec2(0.0f, h)); }

    // A setting with an on/off switch at the right of its label's line and a
    // line of secondary text under it saying what it does.
    bool toggle(const char* id, const char* label_text, const char* description, bool* v)
    {
        using namespace main_window_detail;
        const auto row = ImGui::GetCursorScreenPos();
        ImGui::TextUnformatted(label_text);
        const auto below = ImGui::GetCursorScreenPos();
        const float toggle_w = compact_toggle_width(id, g_dialog_fonts);
        ImGui::SetCursorScreenPos(ImVec2(row.x + _width - toggle_w, row.y + std::floor((ImGui::GetTextLineHeight() - compact_toggle_height(g_dialog_fonts)) * 0.5f)));
        bool clicked = compact_toggle(id, v, g_dialog_fonts);
        ImGui::SetCursorScreenPos(below);
        if(description && *description)
            body(description);
        return clicked;
    }

    // One of several choices, as a card: a title and a line saying what it
    // means, with the selected one outlined in accent. Returns true when clicked.
    bool option(const char* id, const char* title, const char* description, bool selected)
    {
        using namespace r_ui_utils::theme;
        const float pad = 12.0f;
        const auto p = ImGui::GetCursorScreenPos();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + _width - pad * 2.0f);
        const float desc_h = description && *description ? ImGui::CalcTextSize(description, nullptr, false, _width - pad * 2.0f).y : 0.0f;
        ImGui::PopTextWrapPos();
        const float h = pad + ImGui::GetTextLineHeight() + (desc_h > 0.0f ? 4.0f + desc_h : 0.0f) + pad;

        bool clicked = ImGui::InvisibleButton(id, ImVec2(_width, h));
        const bool hovered = ImGui::IsItemHovered();
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + _width, p.y + h), ImGui::GetColorU32(hovered ? surface_raised : surface_card));
        dl->AddRect(p, ImVec2(p.x + _width, p.y + h), ImGui::GetColorU32(selected ? accent : border_subtle), 0.0f, 0, selected ? 2.0f : 1.0f);
        dl->AddText(ImVec2(p.x + pad, p.y + pad), ImGui::GetColorU32(text_primary), title);
        if(desc_h > 0.0f)
        {
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + pad, p.y + pad + ImGui::GetTextLineHeight() + 4.0f),
                        ImGui::GetColorU32(text_secondary), description, nullptr, _width - pad * 2.0f);
        }
        return clicked;
    }

    // A file path in secondary text, cut in the middle when it's too wide so
    // both the folder and the file name stay readable; the full path shows on
    // hover. An empty path shows placeholder in tertiary text. With a button
    // label, that button sits at the right; returns true when it's clicked.
    bool path(const std::string& p, const char* button_label = nullptr, const char* placeholder = "")
    {
        using namespace r_ui_utils::theme;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 4.0f));
        const float bw = button_label ? button_size(button_label).x : 0.0f;
        const float max_w = _width - (button_label ? bw + 12.0f : 0.0f);
        std::string shown = p;
        if(ImGui::CalcTextSize(p.c_str()).x > max_w)
        {
            const std::string dots = "...";  // the fonts have no U+2026
            size_t keep_left = p.size() / 2, keep_right = p.size() - keep_left;
            while((keep_left > 1 || keep_right > 1) &&
                  ImGui::CalcTextSize((p.substr(0, keep_left) + dots + p.substr(p.size() - keep_right)).c_str()).x > max_w)
            {
                if(keep_left >= keep_right) --keep_left;
                else --keep_right;
            }
            shown = p.substr(0, keep_left) + dots + p.substr(p.size() - keep_right);
        }
        if(button_label)
            ImGui::AlignTextToFramePadding();
        if(p.empty())
            ImGui::TextColored(text_tertiary, "%s", placeholder);
        else ImGui::TextColored(text_secondary, "%s", shown.c_str());
        if(shown != p && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", p.c_str());
        bool clicked = false;
        if(button_label)
        {
            ImGui::SameLine(ImGui::GetCursorPosX() + _width - bw);
            clicked = styled_button(button_label);
        }
        ImGui::PopStyleVar();
        return clicked;
    }

    // A thin progress bar across the dialog, in accent.
    void progress(float fraction)
    {
        using namespace r_ui_utils::theme;
        auto p = ImGui::GetCursorScreenPos();
        const float h = 6.0f;
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + _width, p.y + h), ImGui::GetColorU32(surface_sunken));
        dl->AddRectFilled(p, ImVec2(p.x + _width * std::clamp(fraction, 0.0f, 1.0f), p.y + h), ImGui::GetColorU32(accent));
        ImGui::Dummy(ImVec2(_width, h));
    }

    // The action row: an optional left button (e.g. Remove, Back) and the
    // right-aligned buttons, the last of which is the main action. Returns the
    // index of the right button clicked, left, or none.
    int actions(std::initializer_list<button> right, const button* left_button = nullptr)
    {
        gap(8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
        const float x0 = ImGui::GetCursorPosX();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        float total = 0.0f;
        for(const auto& b : right)
            total += button_size(b.label).x + (total > 0.0f ? spacing : 0.0f);

        int clicked = none;
        if(left_button)
        {
            ImGui::BeginDisabled(!left_button->enabled);
            if(styled_button(left_button->label, left_button->kind))
                clicked = left;
            ImGui::EndDisabled();
            ImGui::SameLine(x0 + _width - total);
        }
        else
            ImGui::SetCursorPosX(x0 + _width - total);

        int i = 0;
        for(const auto& b : right)
        {
            if(i > 0)
                ImGui::SameLine();
            ImGui::BeginDisabled(!b.enabled);
            if(styled_button(b.label, b.kind))
                clicked = i;
            ImGui::EndDisabled();
            ++i;
        }
        ImGui::PopStyleVar(2);
        return clicked;
    }

    // Enter in a field, or Enter anywhere in the dialog.
    bool enter() const
    {
        return _enter || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                          (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)));
    }

    bool escape() const { return ImGui::IsKeyPressed(ImGuiKey_Escape); }

    void close() { ImGui::CloseCurrentPopup(); }

private:
    void wrapped(const ImVec4& color, const std::string& text)
    {
        // Not wrapped_text(): it wraps at the window's edge, and an
        // auto-resizing popup grows to fit, so that wouldn't wrap.
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + _width);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }

    float _width;
    bool _open {false};
    bool _appearing {false};
    bool _enter {false};
};

// ---- Local web password ----

// The local web password dialog (the web interface's login password; stored
// as the "system password"). Remove password sits at the left when one is
// set, with an inline confirm. ok_cb(password) when the fields are non-empty
// and match.
template<typename OK_CB, typename CANCEL_CB, typename DELETE_CB>
void web_password_modal(const char* popup_name, bool password_is_set, OK_CB ok_cb, CANCEL_CB cancel_cb, DELETE_CB delete_cb)
{
    namespace txt = web_password_text;
    dialog_frame d(popup_name, 420.0f);
    if(!d)
        return;

    static char password[64] = {0};
    static char confirm[64] = {0};
    static bool tried = false;
    static bool confirming_remove = false;
    if(d.appearing())
    {
        password[0] = '\0';
        confirm[0] = '\0';
        tried = false;
        confirming_remove = false;
    }
    auto close = [&]() {
        password[0] = '\0';
        confirm[0] = '\0';
        d.close();
    };

    d.body(txt::description);
    d.gap(4.0f);
    d.field(txt::password_label, "##password", password, sizeof(password), ImGuiInputTextFlags_Password, nullptr, true);
    d.field(txt::confirm_label, "##confirm", confirm, sizeof(confirm), ImGuiInputTextFlags_Password);

    const bool empty = password[0] == '\0';
    const bool mismatch = std::string(password) != std::string(confirm);
    if(tried && (empty || mismatch))
        d.error(empty ? txt::error_empty : txt::error_mismatch);

    if(confirming_remove)
    {
        d.gap(4.0f);
        d.error(txt::remove_confirm);
        int a = d.actions({{txt::remove_confirm_no}, {txt::remove_confirm_yes, button_kind::destructive}});
        if(a == 0 || d.escape())
            confirming_remove = false;
        else if(a == 1)
        {
            close();
            delete_cb();
        }
        return;
    }

    dialog_frame::button remove {txt::remove, button_kind::ghost_destructive};
    int a = d.actions({{txt::cancel}, {txt::save, button_kind::primary}}, password_is_set ? &remove : nullptr);
    if(a == dialog_frame::left)
        confirming_remove = true;
    else if(a == 0 || d.escape())
    {
        close();
        cancel_cb();
    }
    else if(a == 1 || d.enter())
    {
        tried = true;
        if(!empty && !mismatch)
        {
            std::string pw(password);
            close();
            ok_cb(pw);
        }
    }
}

// ---- RTSP transport ----

// How Revere receives the camera's video: TCP (the default) or UDP, as two
// options with what each is for. transport is "TCP" or "UDP".
inline void rtsp_transport_options(dialog_frame& d, std::string& transport)
{
    d.label("Connection");
    d.gap(2.0f);
    if(d.option("##transport_tcp", "TCP (recommended)",
        "Reliable on Wi-Fi and busy networks.",
        transport != "UDP"))
        transport = "TCP";
    d.gap(2.0f);
    if(d.option("##transport_udp", "UDP",
        "Lower latency on a good wired network, but drops or corrupts video when packets are lost.",
        transport == "UDP"))
        transport = "UDP";
}

// ---- Camera properties ----

// Detection settings first (each a switch with a line saying what it does),
// then a Storage section with where the recording lives and the two storage
// operations. The hours field only matters when still-video pruning is on, so
// it sits under that switch and is disabled otherwise. ok_cb runs with
// min_continuous_retention_hours holding a whole number of hours, 1 or more.
// motion_tier is "auto", "1", "2" or "3" (see r_vss::r_tier_controller);
// tier_status says what the camera uses now and why.
// rtsp_transport is "TCP" or "UDP"; transport_status, when not empty, says
// the camera is on UDP because it refused TCP.
template<typename OK_CB, typename CANCEL_CB, typename MOVE_STORAGE_CB, typename CHANGE_RETENTION_CB>
void camera_properties_modal(
    const char* popup_name,
    const std::string& camera_name,
    bool& do_motion_detection,
    bool& do_motion_pruning,
    std::string& min_continuous_retention_hours,
    std::string& motion_tier,
    const std::string& tier_status,
    std::string& rtsp_transport,
    const std::string& transport_status,
    const std::string& record_file_path,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb,
    MOVE_STORAGE_CB move_storage_cb,
    CHANGE_RETENTION_CB change_retention_cb
)
{
    using namespace r_ui_utils::theme;
    dialog_frame d(popup_name, 460.0f);
    if(!d)
        return;

    static char hours[16] = {0};
    static bool tried = false;
    if(d.appearing())
    {
        r_ui_utils::copy_s(hours, sizeof(hours), min_continuous_retention_hours);
        tried = false;
    }

    if(!camera_name.empty())
        d.heading(camera_name);

    d.toggle("##motion", "Motion detection",
        "Finds movement in this camera's video and marks it as motion events you can search and review.",
        &do_motion_detection);
    if(!do_motion_detection)
        do_motion_pruning = false;

    ImGui::BeginDisabled(!do_motion_detection);
    d.gap(4.0f);
    d.label("Motion and objects from");
    struct tier_choice { const char* value; const char* title; const char* description; };
    static const tier_choice choices[] = {
        {"auto", "Automatic", "The camera's own detection once Revere has checked it reports what Revere sees; Revere's otherwise."},
        {"1", "Revere", "Revere finds motion and objects in the video. Uses the most CPU."},
        {"2", "Camera", "The camera's motion and its person, vehicle and animal events. Uses the least CPU."},
        {"3", "Camera motion, Revere objects", "The camera's motion events; Revere finds the objects in them."}
    };
    for(auto& c : choices)
    {
        d.gap(2.0f);
        if(d.option((std::string("##tier") + c.value).c_str(), c.title, c.description, motion_tier == c.value))
            motion_tier = c.value;
    }
    if(!tier_status.empty())
    {
        d.gap(2.0f);
        d.body(tier_status);
    }

    d.gap(4.0f);
    d.toggle("##prune", "Keep only motion in older video",
        "Video older than the time below is deleted unless it has motion in it, so motion is kept for longer in the same space.",
        &do_motion_pruning);
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!do_motion_pruning);
    d.gap(2.0f);
    d.label("Keep continuous video for at least");
    d.input("##hours", hours, sizeof(hours), ImGuiInputTextFlags_CharsDecimal, 80.0f);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(text_secondary, "hours");
    ImGui::EndDisabled();

    std::string hours_s(hours);
    const bool hours_ok = !hours_s.empty() && hours_s.size() <= 6 &&
                          std::all_of(hours_s.begin(), hours_s.end(), [](unsigned char c) { return std::isdigit(c); }) &&
                          std::stol(hours_s) >= 1;
    if(tried && do_motion_pruning && !hours_ok)
        d.error("Enter a whole number of hours, 1 or more.");

    d.section("Network");
    rtsp_transport_options(d, rtsp_transport);
    if(!transport_status.empty())
    {
        d.gap(2.0f);
        d.body(transport_status, r_ui_utils::theme::status_warning_text);
    }

    d.section("Storage");
    if(!record_file_path.empty())
    {
        d.label("Recording file");
        if(d.path(record_file_path, "Copy path"))
            ImGui::SetClipboardText(record_file_path.c_str());
    }

    bool closing = false;
    auto storage_action = [&](const char* label, const char* description) {
        d.gap(2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 4.0f));
        const float bw = button_size("Change retention").x;  // the wider label, so both line up
        bool clicked = ImGui::Button(label, ImVec2(bw, 0.0f));
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + d.width() - bw - 12.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(text_secondary, "%s", description);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleVar();
        return clicked;
    };
    if(storage_action("Move storage", "Move this camera's recording files to another folder or disk."))
    {
        closing = true;
        d.close();
        move_storage_cb();
    }
    if(!closing && storage_action("Change retention", "Resize the recording file to hold more or fewer days of video. Clears this camera's existing recordings."))
    {
        closing = true;
        d.close();
        change_retention_cb();
    }

    int a = d.actions({{"Cancel"}, {"Save", button_kind::primary}});
    if(closing)
        return;
    if(a == 0 || d.escape())
    {
        d.close();
        cancel_cb();
    }
    else if(a == 1 || d.enter())
    {
        tried = true;
        // The hours only apply with pruning on; leave a bad value alone otherwise.
        if(hours_ok || !do_motion_pruning)
        {
            if(hours_ok)
                min_continuous_retention_hours = hours_s;
            d.close();
            ok_cb();
        }
    }
}

// ---- Remove camera ----

template<typename DELETE_FILES_CB, typename KEEP_FILES_CB, typename CANCEL_CB>
void remove_camera_modal(
    ImGuiContext*,
    const std::string& name,
    const std::string& camera_name,
    bool& delete_files,
    DELETE_FILES_CB delete_files_cb,
    KEEP_FILES_CB keep_files_cb,
    CANCEL_CB cancel_cb
)
{
    dialog_frame d(name.c_str(), 420.0f);
    if(!d)
        return;

    d.heading(camera_name);
    d.body("Revere stops recording this camera and removes it from the list.");
    d.gap(6.0f);
    d.toggle("##delete_files", "Also delete its recordings",
        "Permanently deletes this camera's video and motion data from disk. This can't be undone.",
        &delete_files);

    int a = d.actions({{"Cancel"}, {delete_files ? "Remove and delete" : "Remove camera", button_kind::destructive}});
    if(a == 0 || d.escape())
    {
        d.close();
        cancel_cb();
    }
    else if(a == 1 || d.enter())
    {
        d.close();
        if(delete_files)
            delete_files_cb();
        else keep_files_cb();
    }
}

// ---- Error ----

template<typename OK_CB>
void error_modal(
    ImGuiContext*,
    const std::string& name,
    const std::string& msg,
    OK_CB ok_cb
)
{
    dialog_frame d(name.c_str(), 400.0f);
    if(!d)
        return;

    d.body(msg, r_ui_utils::theme::text_primary);
    int a = d.actions({{"OK", button_kind::primary}});
    if(a == 0 || d.enter() || d.escape())
    {
        d.close();
        ok_cb();
    }
}

// ---- Add RTSP camera ----

// A camera that isn't discovered on the network but has an RTSP stream.
// ok_cb with rscc filled in once the URL looks like an RTSP URL.
template<typename OK_CB, typename CANCEL_CB>
void configure_rtsp_source_camera_modal(
    ImGuiContext*,
    const std::string& name,
    rtsp_source_camera_config& rscc,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    dialog_frame d(name.c_str(), 440.0f);
    if(!d)
        return;

    static char model[128] = {0};
    static char ipv4[64] = {0};
    static char url[512] = {0};
    static char username[128] = {0};
    static char password[128] = {0};
    static bool tried = false;
    if(d.appearing())
    {
        r_ui_utils::copy_s(model, sizeof(model), rscc.camera_name);
        r_ui_utils::copy_s(ipv4, sizeof(ipv4), rscc.ipv4);
        r_ui_utils::copy_s(url, sizeof(url), rscc.rtsp_url);
        r_ui_utils::copy_s(username, sizeof(username), rscc.rtsp_username);
        r_ui_utils::copy_s(password, sizeof(password), rscc.rtsp_password);
        tried = false;
    }

    d.body("For a camera Revere can't find on the network but that has an RTSP stream. Give it a fixed IP address on your router, so the URL keeps working.");
    d.gap(4.0f);
    d.field("Camera name", "##model", model, sizeof(model), 0, "e.g. Back door", true);
    d.field("IP address", "##ipv4", ipv4, sizeof(ipv4), 0, "192.168.1.20");
    d.field("RTSP URL", "##url", url, sizeof(url), 0, "rtsp://192.168.1.20:554/stream1");
    d.field("Username", "##username", username, sizeof(username), 0, "Optional");
    d.field("Password", "##password", password, sizeof(password), ImGuiInputTextFlags_Password, "Optional");
    d.gap(4.0f);
    rtsp_transport_options(d, rscc.rtsp_transport);

    std::string url_s(url);
    const bool url_ok = url_s.rfind("rtsp://", 0) == 0 || url_s.rfind("rtsps://", 0) == 0;
    const bool name_ok = model[0] != '\0';
    if(tried && !name_ok)
        d.error("Enter a name for the camera.");
    else if(tried && !url_ok)
        d.error("Enter the camera's RTSP URL, starting with rtsp://");

    int a = d.actions({{"Cancel"}, {"Add camera", button_kind::primary}});
    if(a == 0 || d.escape())
    {
        d.close();
        cancel_cb();
    }
    else if(a == 1 || d.enter())
    {
        tried = true;
        if(name_ok && url_ok)
        {
            rscc.camera_name = model;
            rscc.ipv4 = ipv4;
            rscc.rtsp_url = url_s;
            rscc.rtsp_username = username;
            rscc.rtsp_password = password;
            password[0] = '\0';
            d.close();
            ok_cb();
        }
    }
}

// ---- Add camera wizard ----
// Each step's popup name is assignment_state::step_title(heading): the title
// bar says "Add camera · step N of M" and the heading is drawn inside.

template<typename OK_CB, typename CANCEL_CB>
void rtsp_credentials_modal(
    ImGuiContext*,
    const std::string& name,
    r_utils::r_nullable<std::string>& u,
    r_utils::r_nullable<std::string>& p,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    dialog_frame d(name.c_str(), 420.0f);
    if(!d)
        return;

    static char username[128] = {0};
    static char password[128] = {0};
    if(d.appearing())
    {
        username[0] = '\0';
        password[0] = '\0';
        u = r_utils::r_nullable<std::string>();
        p = r_utils::r_nullable<std::string>();
    }

    d.step_heading(name);
    d.body("The user name and password Revere should use to talk to this camera (its ONVIF user). They're often the same as the camera's own web login.");
    d.gap(4.0f);
    d.field("Username", "##username", username, sizeof(username), 0, nullptr, true);
    u.set_value(std::string(username));
    d.field("Password", "##password", password, sizeof(password), ImGuiInputTextFlags_Password);
    p.set_value(std::string(password));

    int a = d.actions({{"Cancel"}, {"Next", button_kind::primary}});
    if(a == 0 || d.escape())
    {
        password[0] = '\0';
        d.close();
        cancel_cb();
    }
    else if(a == 1 || d.enter())
    {
        password[0] = '\0';
        d.close();
        ok_cb();
    }
}

// While Revere talks to the camera: what it's doing and for how long.
template<typename CANCEL_CB>
void please_wait_modal(
    ImGuiContext*,
    const std::string& name,
    const std::string& status_line,  // current step description (empty = hide)
    CANCEL_CB cancel_cb
)
{
    dialog_frame d(name.c_str(), 420.0f);
    if(!d)
        return;

    d.step_heading(name);
    d.body("Talking to the camera. This usually takes a few seconds.", r_ui_utils::theme::text_primary);
    if(!status_line.empty())
        d.body(status_line);

    // An indeterminate bar: a block that slides along the track.
    d.gap(4.0f);
    {
        using namespace r_ui_utils::theme;
        auto p = ImGui::GetCursorScreenPos();
        const float h = 4.0f;
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + d.width(), p.y + h), ImGui::GetColorU32(surface_sunken));
        const float block = d.width() * 0.25f;
        const float t = (float)std::fmod(ImGui::GetTime(), 1.6) / 1.6f;
        const float x = p.x + (d.width() + block) * t - block;
        dl->AddRectFilled(ImVec2(std::max(p.x, x), p.y), ImVec2(std::min(p.x + d.width(), x + block), p.y + h), ImGui::GetColorU32(accent));
        ImGui::Dummy(ImVec2(d.width(), h));
    }

    int a = d.actions({{"Cancel"}});
    if(a == 0 || d.escape())
    {
        d.close();
        cancel_cb();
    }
}

// Which of the camera's streams to record, as cards.
template<typename NEXT_CB, typename CANCEL_CB>
void profile_selection_modal(
    const std::string& name,
    revere::assignment_state& as,
    NEXT_CB next_cb,
    CANCEL_CB cancel_cb
)
{
    dialog_frame d(name.c_str(), 420.0f);
    if(!d)
        return;

    d.step_heading(name);
    d.body("Cameras usually offer a full-resolution stream and smaller ones. Pick the one to record.");
    if(as.profiles_were_filtered)
        d.body("Streams in formats Revere can't record aren't listed.", r_ui_utils::theme::status_warning_text);
    d.gap(4.0f);

    for(int i = 0; i < (int)as.available_profiles.size(); ++i)
    {
        const auto& pr = as.available_profiles[i];
        auto title = main_window_detail::upper(pr.encoding) + "  \xC2\xB7  " + std::to_string(pr.width) + " \xC3\x97 " + std::to_string(pr.height);
        ImGui::PushID(i);
        if(d.option("##profile", title.c_str(), i == 0 ? "Usually the main, highest-quality stream." : "", i == as.selected_profile_idx))
            as.selected_profile_idx = i;
        ImGui::PopID();
    }

    d.gap(6.0f);
    rtsp_transport_options(d, as.rtsp_transport);

    const bool have = !as.available_profiles.empty();
    int a = d.actions({{"Cancel"}, {"Next", button_kind::primary, have}});
    if(a == 0 || d.escape())
    {
        d.close();
        cancel_cb();
    }
    else if(have && (a == 1 || d.enter()))
    {
        as.selected_profile_token = as.available_profiles[std::clamp(as.selected_profile_idx, 0, (int)as.available_profiles.size() - 1)].token;
        d.close();
        next_cb();
    }
}

// The camera's picture and the name it'll have in Revere.
template<typename OK_CB, typename CANCEL_CB>
void friendly_name_modal(
    ImGuiContext*,
    const std::string& name,
    revere::assignment_state& as,
    std::string& fn,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    dialog_frame d(name.c_str(), 440.0f);
    if(!d)
        return;

    static char friendly_name[128] = {0};
    static bool tried = false;
    if(d.appearing())
    {
        r_ui_utils::copy_s(friendly_name, sizeof(friendly_name), fn);
        tried = false;
    }

    d.step_heading(name);
    if(as.key_frame_texture)
    {
        const auto& t = *as.key_frame_texture;
        const float aspect = (t.width() > 0 && t.height() > 0) ? (float)t.height() / (float)t.width() : 0.75f;
        ImGui::Image(t.imgui_id(), ImVec2(d.width(), std::floor(d.width() * aspect)));
    }
    d.field("Name", "##friendly_name", friendly_name, sizeof(friendly_name), 0, "e.g. Front door", true);
    fn = friendly_name;
    if(tried && fn.empty())
        d.error("Enter a name for the camera.");

    int a = d.actions({{"Cancel"}, {"Next", button_kind::primary}});
    if(a == 0 || d.escape())
    {
        d.close();
        cancel_cb();
    }
    else if(a == 1 || d.enter())
    {
        tried = true;
        if(!fn.empty())
        {
            d.close();
            ok_cb();
        }
    }
}

template<typename OK_CB, typename CANCEL_CB>
void motion_detection_modal(
    ImGuiContext*,
    const std::string& name,
    bool& do_motion_detection,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    dialog_frame d(name.c_str(), 420.0f);
    if(!d)
        return;

    d.step_heading(name);
    d.toggle("##motion", "Motion detection",
        "Finds movement in this camera's video and marks it as motion events you can search and review. You can change this later in the camera's properties.",
        &do_motion_detection);

    int a = d.actions({{"Cancel"}, {"Next", button_kind::primary}});
    if(a == 0 || d.escape())
    {
        d.close();
        cancel_cb();
    }
    else if(a == 1 || d.enter())
    {
        d.close();
        ok_cb();
    }
}

// A new recording file, or one that's already on disk.
template<typename NEW_CB, typename EXISTING_CB, typename CANCEL_CB>
void new_or_existing_modal(
    ImGuiContext*,
    const std::string& name,
    NEW_CB new_cb,
    EXISTING_CB existing_cb,
    CANCEL_CB cancel_cb
)
{
    dialog_frame d(name.c_str(), 420.0f);
    if(!d)
        return;

    static int choice = 0;
    if(d.appearing())
        choice = 0;

    d.step_heading(name);
    d.body("Revere records each camera into one file of a fixed size, reusing the oldest space when it's full.");
    d.gap(4.0f);
    if(d.option("##new", "New recording file", "Create a file sized for how many days of video to keep.", choice == 0))
        choice = 0;
    if(d.option("##existing", "Existing recording file", "Use a .nts file this camera recorded to before, e.g. after reinstalling.", choice == 1))
        choice = 1;

    int a = d.actions({{"Cancel"}, {"Next", button_kind::primary}});
    if(a == 0 || d.escape())
    {
        d.close();
        cancel_cb();
    }
    else if(a == 1 || d.enter())
    {
        d.close();
        if(choice == 0)
            new_cb();
        else existing_cb();
    }
}

template<typename DEFAULT_CB, typename CUSTOM_CB, typename CANCEL_CB>
void storage_location_modal(
    ImGuiContext*,
    const std::string& name,
    const std::string& default_path,
    DEFAULT_CB default_cb,
    CUSTOM_CB custom_cb,
    CANCEL_CB cancel_cb
)
{
    dialog_frame d(name.c_str(), 420.0f);
    if(!d)
        return;

    static int choice = 0;
    if(d.appearing())
        choice = 0;

    d.step_heading(name);
    d.body("Where should this camera's recording file go?");
    d.gap(4.0f);
    if(d.option("##default", "Revere's video folder", default_path.c_str(), choice == 0))
        choice = 0;
    if(d.option("##custom", "Another folder...", "Pick a folder, e.g. on a larger disk.", choice == 1))
        choice = 1;

    int a = d.actions({{"Cancel"}, {"Next", button_kind::primary}});
    if(a == 0 || d.escape())
    {
        d.close();
        cancel_cb();
    }
    else if(a == 1 || d.enter())
    {
        d.close();
        if(choice == 0)
            default_cb();
        else custom_cb();
    }
}

// How many days to keep, and the file size that takes at the camera's bitrate.
template<typename OK_CB, typename CANCEL_CB>
void retention_modal(
    ImGuiContext*,
    const std::string& name,
    revere::assignment_state& as,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    using namespace r_ui_utils::theme;
    dialog_frame d(name.c_str(), 420.0f);
    if(!d)
        return;

    d.step_heading(name);
    d.body(as.camera_friendly_name + "  \xC2\xB7  " + std::to_string((as.byte_rate * 8) / 1024) + " kbps");
    d.body("The recording file is sized for the days of video to keep at this bitrate. When it's full, the oldest video is replaced.");
    d.gap(4.0f);
    d.label("Keep video for");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputDouble("##days", &as.continuous_retention_days, 0.0, 0.0, "%.1f");
    ImGui::PopStyleVar();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(text_secondary, "days");
    if(as.continuous_retention_days < 0.0)
        as.continuous_retention_days = 0.0;

    auto sz_info = r_storage::required_file_size_for_retention_hours((int64_t)(as.continuous_retention_days * 24.0), as.byte_rate);
    as.num_storage_file_blocks.set_value(sz_info.first);
    as.storage_file_block_size.set_value(sz_info.second);
    auto human = r_storage::human_readable_file_size((double)(as.num_storage_file_blocks.value() * as.storage_file_block_size.value()));
    d.body("Needs " + human + " of disk space.");

    const bool ok = as.continuous_retention_days > 0.0;
    int a = d.actions({{"Cancel"}, {"Next", button_kind::primary, ok}});
    if(a == 0 || d.escape())
    {
        d.close();
        cancel_cb();
    }
    else if(ok && (a == 1 || d.enter()))
    {
        d.close();
        ok_cb();
    }
}

// The recording file's name; the last step, so its button adds the camera.
template<typename OK_CB, typename CANCEL_CB>
void new_file_name_modal(
    ImGuiContext*,
    const std::string& name,
    std::string& file_name,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    dialog_frame d(name.c_str(), 420.0f);
    if(!d)
        return;

    static char new_file_name[128] = {0};
    static std::string last_file_name;
    static bool tried = false;
    if(d.appearing() || last_file_name != file_name)
    {
        r_ui_utils::copy_s(new_file_name, sizeof(new_file_name), file_name);
        last_file_name = file_name;
        if(d.appearing())
            tried = false;
    }

    d.step_heading(name);
    d.body("The name of this camera's recording file. The default is fine unless you keep files from several installs in one folder.");
    d.field("File name", "##file_name", new_file_name, sizeof(new_file_name), 0, nullptr, true);
    file_name = new_file_name;
    last_file_name = file_name;
    if(tried && file_name.empty())
        d.error("Enter a file name.");

    int a = d.actions({{"Cancel"}, {"Add camera", button_kind::primary}});
    if(a == 0 || d.escape())
    {
        d.close();
        cancel_cb();
    }
    else if(a == 1 || d.enter())
    {
        tried = true;
        if(!file_name.empty())
        {
            d.close();
            ok_cb();
        }
    }
}

// ---- Storage operations (from camera properties, and Repair) ----

// While a storage operation runs: what's happening, a progress bar and the
// worker's status line.
inline int storage_progress(dialog_frame& d, const char* what, float progress, const std::string& status, bool cancellable)
{
    d.body(what, r_ui_utils::theme::text_primary);
    d.gap(2.0f);
    d.progress(progress);
    if(!status.empty())
        d.body(status);
    return cancellable ? d.actions({{"Cancel"}}) : dialog_frame::none;
}

// Resize a camera's recording file for a new number of days. The file is a
// fixed-size ring that can't grow in place, so this recreates it and the
// camera's existing recordings are deleted; Apply waits for that to be
// acknowledged.
template<typename START_CB, typename CLOSE_CB, typename CANCEL_OP_CB>
void change_retention_modal(
    ImGuiContext*,
    const std::string& name,
    const std::string& camera_name,
    int64_t byte_rate,
    double& retention_days,
    bool& confirm_delete,
    int64_t& out_num_blocks,
    int64_t& out_block_size,
    float progress,
    bool running,
    bool done,
    bool error,
    const std::string& status,
    START_CB start_cb,
    CLOSE_CB close_cb,
    CANCEL_OP_CB cancel_op_cb
)
{
    using namespace r_ui_utils::theme;
    dialog_frame d(name.c_str(), 440.0f);
    if(!d)
        return;

    d.heading(camera_name);
    if(!running && !done)
    {
        if(d.appearing())
            confirm_delete = false;

        const bool have_bitrate = byte_rate > 0;
        if(have_bitrate)
            d.body("Current bitrate: " + std::to_string((byte_rate * 8) / 1024) + " kbps. The recording file is sized for the days of video to keep at this bitrate.");
        else
        {
            d.body("This camera isn't receiving video, so there's no bitrate to size the file from.", status_danger_text);
            d.body("Retention can be changed once the camera is recording.");
        }

        d.gap(4.0f);
        ImGui::BeginDisabled(!have_bitrate);
        d.label("Keep video for");
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputDouble("##days", &retention_days, 0.0, 0.0, "%.1f");
        ImGui::PopStyleVar();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(text_secondary, "days");
        if(retention_days < 0.0)
            retention_days = 0.0;
        ImGui::EndDisabled();

        if(have_bitrate)
        {
            auto sz = r_storage::required_file_size_for_retention_hours((int64_t)(retention_days * 24.0), byte_rate);
            out_num_blocks = sz.first;
            out_block_size = sz.second;
            d.body("New file size: " + r_storage::human_readable_file_size((double)(out_num_blocks * out_block_size)) + ".");
        }

        d.gap(6.0f);
        d.body("Resizing recreates the recording file, which permanently deletes this camera's existing recordings.", status_danger_text);
        d.gap(2.0f);
        d.toggle("##confirm", "Delete the existing recordings", nullptr, &confirm_delete);

        const bool can_start = have_bitrate && retention_days > 0.0 && confirm_delete;
        int a = d.actions({{"Cancel"}, {"Resize", button_kind::destructive, can_start}});
        if(a == 0 || d.escape())
        {
            d.close();
            close_cb();
        }
        else if(can_start && (a == 1 || d.enter()))
            start_cb();
    }
    else if(running)
    {
        if(storage_progress(d, "Resizing the recording file...", progress, status, true) == 0)
            cancel_op_cb();
    }
    else if(done && !error)
    {
        d.close();
        close_cb();
    }
    else if(error)
    {
        d.body("Couldn't resize the recording file.", status_danger_text);
        d.body(status);
        int a = d.actions({{"Close", button_kind::primary}});
        if(a == 0 || d.enter() || d.escape())
        {
            d.close();
            close_cb();
        }
    }
}

// Move a camera's recording files to another folder, or start over there with
// empty files.
template<typename START_CB, typename CLOSE_CB, typename CANCEL_OP_CB>
void move_storage_modal(
    ImGuiContext*,
    const std::string& name,
    const std::string& camera_name,
    std::string& new_dir,
    bool& start_fresh,
    bool& delete_confirmed,
    float progress,
    bool running,
    bool done,
    bool error,
    const std::string& status,
    START_CB start_cb,
    CLOSE_CB close_cb,
    CANCEL_OP_CB cancel_op_cb
)
{
    using namespace r_ui_utils::theme;
    dialog_frame d(name.c_str(), 440.0f);
    if(!d)
        return;

    d.heading(camera_name);
    if(!running && !done)
    {
        if(d.appearing())
        {
            start_fresh = false;
            delete_confirmed = false;
        }

        d.label("Move to");
        if(d.path(new_dir, "Choose folder...", "No folder chosen"))
            ImGuiFileDialog::Instance()->OpenDialog("MoveStorageDirDlg", "Choose destination", nullptr, new_dir.empty() ? "." : new_dir, "", 1, nullptr, 0);
        if(ImGuiFileDialog::Instance()->Display("MoveStorageDirDlg", ImGuiWindowFlags_None, ImVec2(800, 400)))
        {
            if(ImGuiFileDialog::Instance()->IsOk())
                new_dir = ImGuiFileDialog::Instance()->GetCurrentPath();
            ImGuiFileDialog::Instance()->Close();
        }

        d.gap(6.0f);
        d.toggle("##fresh", "Start fresh", "Don't copy the existing recordings; create empty recording files in the new folder.", &start_fresh);
        if(start_fresh)
        {
            d.gap(2.0f);
            d.body("This camera's existing recordings will be permanently deleted.", status_danger_text);
            d.toggle("##confirm", "Delete the existing recordings", nullptr, &delete_confirmed);
        }

        const bool can_start = !new_dir.empty() && (!start_fresh || delete_confirmed);
        int a = d.actions({{"Cancel"}, {"Move", start_fresh ? button_kind::destructive : button_kind::primary, can_start}});
        if(a == 0 || d.escape())
        {
            d.close();
            close_cb();
        }
        else if(can_start && (a == 1 || d.enter()))
            start_cb();
    }
    else if(running)
    {
        if(storage_progress(d, start_fresh ? "Creating new recording files..." : "Moving the recording files...", progress, status, true) == 0)
            cancel_op_cb();
    }
    else if(done && !error)
    {
        d.close();
        close_cb();
    }
    else if(error)
    {
        d.body("Couldn't move the recording files.", status_danger_text);
        d.body(status);
        int a = d.actions({{"Close", button_kind::primary}});
        if(a == 0 || d.enter() || d.escape())
        {
            d.close();
            close_cb();
        }
    }
}

// From a recording camera's Repair button when its recording file is missing.
//   mode 0  Re-create: new, empty files from the camera's stored geometry.
//   mode 1  Locate: point the camera at an existing .nts (moved by hand, or on
//           another disk); its geometry comes from the file, and a motion file
//           next to it is reused or created.
// Either way the stream keeper restarts the camera afterwards.
template<typename START_CB, typename CLOSE_CB>
void repair_storage_modal(
    ImGuiContext*,
    const std::string& name,
    const std::string& camera_name,
    const std::string& missing_path,
    int& mode,
    std::string& locate_path,
    float progress,
    bool running,
    bool done,
    bool error,
    const std::string& status,
    START_CB start_cb,
    CLOSE_CB close_cb
)
{
    using namespace r_ui_utils::theme;
    dialog_frame d(name.c_str(), 460.0f);
    if(!d)
        return;

    d.heading(camera_name);
    if(!running && !done && !error)
    {
        if(d.appearing())
        {
            mode = 0;
            locate_path.clear();
        }

        d.body("This camera's recording file is missing, so it can't record:");
        d.path(missing_path);
        d.gap(4.0f);
        if(d.option("##recreate", "Re-create empty recording files", "New files with the camera's current retention settings. Any leftover files for this camera are deleted first.", mode == 0))
            mode = 0;
        if(d.option("##locate", "Use an existing recording file", "If the camera's .nts file was moved, pick it. Its recordings are kept, and a motion file next to it is reused.", mode == 1))
            mode = 1;

        if(mode == 1)
        {
            d.gap(2.0f);
            d.label("Recording file");
            if(d.path(locate_path, "Choose file...", "No file chosen"))
            {
                std::string start_dir = ".";
                auto slash = missing_path.find_last_of("/\\");
                if(slash != std::string::npos)
                    start_dir = missing_path.substr(0, slash);
                ImGuiFileDialog::Instance()->OpenDialog("RepairLocateFileDlg", "Choose recording file", ".nts", start_dir);
            }
        }
        if(ImGuiFileDialog::Instance()->Display("RepairLocateFileDlg", ImGuiWindowFlags_None, ImVec2(800, 400)))
        {
            if(ImGuiFileDialog::Instance()->IsOk())
                locate_path = ImGuiFileDialog::Instance()->GetFilePathName();
            ImGuiFileDialog::Instance()->Close();
        }

        const bool can_start = mode == 0 || !locate_path.empty();
        int a = d.actions({{"Cancel"}, {"Repair", button_kind::primary, can_start}});
        if(a == 0 || d.escape())
        {
            d.close();
            close_cb();
        }
        else if(can_start && (a == 1 || d.enter()))
            start_cb();
    }
    else if(running)
        storage_progress(d, mode == 1 ? "Checking the recording file..." : "Re-creating the recording files...", progress, status, false);
    else if(done && !error)
    {
        d.close();
        close_cb();
    }
    else if(error)
    {
        d.body("Couldn't repair the recording file.", status_danger_text);
        d.body(status);
        int a = d.actions({{"Close", button_kind::primary}});
        if(a == 0 || d.enter() || d.escape())
        {
            d.close();
            close_cb();
        }
    }
}

// ---- About Revere ----

// Help > About Revere: which build this is, one fact per line, and Copy for
// all of it as one line (for a support email). lines[0] is the heading.
template<typename CLOSE_CB>
void about_revere_modal(
    ImGuiContext*,
    const std::string& name,
    const std::vector<std::string>& lines,
    const std::string& copy_text,
    CLOSE_CB close_cb
)
{
    static bool copied = false;
    dialog_frame d(name.c_str(), 420.0f);
    if(!d)
        return;
    if(d.appearing())
        copied = false;

    if(!lines.empty())
        d.heading(lines[0]);
    for(size_t i = 1; i < lines.size(); ++i)
        d.body(lines[i]);

    dialog_frame::button copy {copied ? "Copied" : "Copy details"};
    int a = d.actions({{"Close", button_kind::primary}}, &copy);
    if(a == dialog_frame::left)
    {
        ImGui::SetClipboardText(copy_text.c_str());
        copied = true;
    }
    else if(a == 0 || d.enter() || d.escape())
    {
        d.close();
        close_cb();
    }
}

}

#endif
