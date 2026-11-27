#ifndef __revere_camera_properties_pane_h
#define __revere_camera_properties_pane_h

#include "camera_properties_state.h"
#include "dialogs.h"

// A camera's properties, edited in the camera's pane (the right-hand detail pane) rather than in a dialog: the sections
// are cards in one column, as wide as reads well (properties_content_width), and scroll; Cancel and Save stay at the
// bottom, at the column's right. Its text is the detail pane's, not the dialogs' larger default.
//
// Sections: Recording (where the recording lives and the two storage operations), Detection (motion, what finds it,
// pruning), Streams and connection (the smaller stream, TCP or UDP, the camera's clock) and Doorbell (whether the
// device is one, how it reports a press, what to type in a Reolink doorbell's app, and its press test).
// Another section is one more `section(...)` call below, and gets its tab (the row of links above the sections, which
// scroll to a section and mark the one in view); room is left in Recording for "Record from".

namespace revere
{

enum class camera_properties_result { none, cancel, save };

// The detection choices: motion_tier is "auto", "1", "2" or "3" (see r_vss::r_tier_controller).
struct tier_choice
{
    const char* value;
    const char* pill;
    const char* description;
};
inline const tier_choice* tier_choices(size_t& n)
{
    static const tier_choice choices[] = {
        {"auto", "Automatic", "The camera's own detection once Revere has checked it reports what Revere sees; Revere's otherwise."},
        {"1", "Revere", "Revere finds motion and objects in the video. Uses the most CPU."},
        {"2", "Camera", "The camera's motion and its person, vehicle and animal events. Uses the least CPU."},
        {"3", "Camera motion, Revere objects", "The camera's motion events; Revere finds the objects in them."}
    };
    n = sizeof(choices) / sizeof(choices[0]);
    return choices;
}

// Draws the properties in the space left in the current window and returns what the user chose at the bottom.
// tier_status says what the camera uses now and why; rtsp_transport is "TCP" or "UDP"; transport_status, when not
// empty, says the camera is on UDP because it refused TCP; streams is the Streams state (see streams_state) and
// streams_cb(op, token) is called for a click in it: op "off" (use the recording stream), "use" (the smaller stream
// with that profile token), "find" (look again) or "analysis" (token "1" or "0"). can_sync_time: the camera was found
// by ONVIF discovery, so its clock can be set. sync_time_cb, move_storage_cb and change_retention_cb open the small
// dialogs for those. On Save with a valid form, min_continuous_retention_hours holds the hours field.
// press_source is the doorbell's ("onvif" or "reolink_email"); door is what the app last read about its presses and its
// email setup (doorbell_view), press_test_cb() starts the press test ("Start test"), and new_password_cb() makes a new
// email password once "New password" is confirmed.
template<typename STREAMS_CB, typename SYNC_TIME_CB, typename MOVE_STORAGE_CB, typename CHANGE_RETENTION_CB, typename PRESS_TEST_CB,
         typename NEW_PASSWORD_CB>
camera_properties_result camera_properties_pane(
    camera_properties_session& session,
    bool& is_doorbell,
    std::string& press_source,
    doorbell_view& door,
    bool& do_motion_detection,
    bool& do_motion_pruning,
    std::string& min_continuous_retention_hours,
    std::string& motion_tier,
    const std::string& tier_status,
    std::string& rtsp_transport,
    const std::string& transport_status,
    const std::string& record_file_path,
    streams_state& streams,
    bool can_sync_time,
    STREAMS_CB streams_cb,
    SYNC_TIME_CB sync_time_cb,
    MOVE_STORAGE_CB move_storage_cb,
    CHANGE_RETENTION_CB change_retention_cb,
    PRESS_TEST_CB press_test_cb,
    NEW_PASSWORD_CB new_password_cb
)
{
    using namespace r_ui_utils::theme;

    bool streams_changed, checking_streams;
    {
        std::lock_guard<std::mutex> g(streams.m);
        streams_changed = streams.changed;
        checking_streams = streams.working;
    }
    // Pruning needs motion detection.
    if(!do_motion_detection)
        do_motion_pruning = false;
    const bool dirty = session.dirty(is_doorbell, do_motion_detection, do_motion_pruning, motion_tier, rtsp_transport, streams_changed, press_source);

    // The row of tabs, one per section, above the sections and so fixed while they scroll: a click scrolls to that
    // section, and the one in view is marked. The sections report where they start as they're drawn (properties_nav),
    // so the row is the last frame's; the row's room is kept before there is one.
    {
        const int active = session.nav.active();
        ImGui::PushFont(main_window_detail::font(g_dialog_fonts.key_18, true));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 6.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, with_alpha(surface_raised, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, with_alpha(surface_raised, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, with_alpha(surface_raised, 0.0f));
        const float row_h = ImGui::GetFrameHeight();
        if(session.nav.titles.empty())
            ImGui::Dummy(ImVec2(0.0f, row_h));
        for(size_t i = 0; i < session.nav.titles.size(); ++i)
        {
            if(i > 0)
                ImGui::SameLine(0.0f, 22.0f);
            const bool here = (int)i == active;
            ImGui::PushStyleColor(ImGuiCol_Text, here ? text_primary : text_tertiary);
            const std::string label = session.nav.titles[i] + "##section_tab_" + std::to_string(i);
            if(ImGui::Button(label.c_str()))
                session.nav.jump = (int)i;
            ImGui::PopStyleColor();
            const bool hovered = ImGui::IsItemHovered();
            if(hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if(here || hovered)
            {
                const ImVec2 lo = ImGui::GetItemRectMin();
                const ImVec2 hi = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddLine(ImVec2(lo.x, hi.y - 1.0f), ImVec2(hi.x, hi.y - 1.0f),
                                                    ImGui::GetColorU32(here ? accent : border_subtle), 2.0f);
            }
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        ImGui::PopFont();
    }

    // The sections' column, measured inside their scrolling child so a scroll bar doesn't cover the cards' right edge.
    float content_w = 0.0f;
    const float footer_h = 62.0f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 4.0f));
    ImGui::BeginChild("##properties_sections", ImVec2(0.0f, std::max(80.0f, avail.y - footer_h)), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    // The detail pane's text (key_22, as a plugin's pane) with explanations in its smaller text (key_18, as the camera
    // pane's notes), not the app's default (key_24, the dialogs' size), which is a size larger than the pane around it.
    // A tab was clicked: scroll to its section.
    if(session.nav.jump >= 0)
    {
        ImGui::SetScrollY(session.nav.scroll_target(session.nav.jump));
        session.nav.jump = -1;
    }
    session.nav.begin_frame();
    ImGui::PushFont(main_window_detail::font(g_dialog_fonts.key_22));
    content_w = std::floor(properties_content_width(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize()));
    {
        // A section: its title, then its rows on a card, with a line between rows. The card's background goes in
        // under its rows once their height is known.
        const float pad = 16.0f;
        const float inner_w = std::max(160.0f, content_w - pad * 2.0f);
        auto section = [&](const char* title, auto&& rows) {
            session.nav.add(title, ImGui::GetCursorPosY());
            ImGui::PushFont(main_window_detail::font(g_dialog_fonts.key_18, true));
            ImGui::TextColored(text_tertiary, "%s", main_window_detail::upper(title).c_str());
            ImGui::PopFont();
            auto* dl = ImGui::GetWindowDrawList();
            ImDrawListSplitter splitter;
            splitter.Split(dl, 2);
            splitter.SetCurrentChannel(dl, 1);
            const ImVec2 top = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(0.0f, pad - 6.0f));
            ImGui::Indent(pad);
            {
                dialog_frame d(dialog_frame::embedded, inner_w);
                rows(d);
            }
            ImGui::Unindent(pad);
            ImGui::Dummy(ImVec2(0.0f, pad - 6.0f));
            const float bottom = ImGui::GetCursorScreenPos().y - 6.0f;
            splitter.SetCurrentChannel(dl, 0);
            dl->AddRectFilled(top, ImVec2(top.x + content_w, bottom), ImGui::GetColorU32(surface_card));
            dl->AddRect(top, ImVec2(top.x + content_w, bottom), ImGui::GetColorU32(border_subtle));
            splitter.Merge(dl);
            ImGui::Dummy(ImVec2(0.0f, 14.0f));
        };
        // The line between two rows of a card, across the card.
        auto divider = [&](dialog_frame& d) {
            d.gap(4.0f);
            const auto p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x - pad, p.y), ImVec2(p.x - pad + content_w, p.y), ImGui::GetColorU32(border_subtle));
            d.gap(4.0f);
        };
        // A button at the right of the text saying what it does, or under the text when the row is too narrow. The
        // row's name, when it has one, is above the text.
        auto action = [&](dialog_frame& d, const char* name, const char* button, const std::string& description) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 5.0f));
            const float bw = main_window_detail::button_size(button).x;
            const bool beside = fits_beside(d.width(), bw, 200.0f);
            const auto row = ImGui::GetCursorScreenPos();
            bool clicked = false;
            if(beside)
            {
                ImGui::SetCursorScreenPos(ImVec2(row.x + d.width() - bw, row.y));
                clicked = main_window_detail::styled_button(button);
                const float button_bottom = ImGui::GetItemRectMax().y;
                // The first line level with the button's label.
                const float first_h = name ? ImGui::GetTextLineHeight() : main_window_detail::font(g_dialog_fonts.key_18)->FontSize;
                ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + std::max(0.0f, std::floor((ImGui::GetItemRectSize().y - first_h) * 0.5f))));
                dialog_frame text(dialog_frame::embedded, d.width() - bw - 16.0f);
                if(name)
                    text.label(name);
                text.body(description);
                const float y = std::max(ImGui::GetCursorScreenPos().y, button_bottom + ImGui::GetStyle().ItemSpacing.y);
                ImGui::SetCursorScreenPos(ImVec2(row.x, y));
            }
            else
            {
                if(name)
                    d.label(name);
                d.body(description);
                clicked = main_window_detail::styled_button(button);
            }
            ImGui::PopStyleVar();
            return clicked;
        };

        auto recording = [&](dialog_frame& d) {
            if(!record_file_path.empty())
            {
                // The file's name and the path, on one line when there's room.
                const float name_w = label_column_width(ImGui::CalcTextSize("Recording file").x, d.width(), 240.0f);
                if(name_w > 0.0f)
                {
                    // Lined up with the path, which sits level with its Copy path button.
                    const auto row = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 4.0f));
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Recording file");
                    ImGui::PopStyleVar();
                    ImGui::SameLine();
                    ImGui::SetCursorScreenPos(ImVec2(row.x + name_w, row.y));
                }
                else
                    d.label("Recording file");
                dialog_frame value(dialog_frame::embedded, d.width() - name_w);
                if(value.path(record_file_path, "Copy path"))
                    ImGui::SetClipboardText(record_file_path.c_str());
                divider(d);
            }
            if(action(d, nullptr, "Move storage", "Move this camera's recording files to another folder or disk."))
                move_storage_cb();
            divider(d);
            if(action(d, nullptr, "Change retention", "Resize the recording file to hold more or fewer days of video. Clears this camera's existing recordings."))
                change_retention_cb();
            // "Record from" (the stream recorded) goes here.
        };

        auto detection = [&](dialog_frame& d) {
            d.toggle("##motion", "Motion detection",
                "Finds movement in this camera's video and marks it as motion events you can search and review.",
                &do_motion_detection);
            divider(d);

            ImGui::BeginDisabled(!do_motion_detection);
            d.label("Motion and objects from");
            d.gap(2.0f);
            size_t n = 0;
            const auto* choices = tier_choices(n);
            const char* chosen_description = "";
            for(size_t i = 0; i < n; ++i)
            {
                const bool selected = motion_tier == choices[i].value;
                if(selected)
                    chosen_description = choices[i].description;
                if(d.pill((std::string("##tier") + choices[i].value).c_str(), choices[i].pill, selected, i == 0))
                    motion_tier = choices[i].value;
            }
            d.gap(2.0f);
            d.body(chosen_description);
            if(!tier_status.empty())
                d.body(tier_status);
            divider(d);

            d.toggle("##prune", "Keep only motion in older video",
                "Video older than the time below is deleted unless it has motion in it, so motion is kept for longer in the same space.",
                &do_motion_pruning);
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!do_motion_pruning);
            d.gap(2.0f);
            {
                // The hours, after their label on one line when there's room.
                const float label_w = ImGui::CalcTextSize("Keep continuous video for at least").x;
                const float field_w = 80.0f;
                const float unit_w = ImGui::CalcTextSize("hours").x;
                const bool one_line = fits_beside(d.width(), field_w + 8.0f + unit_w, label_w);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(text_secondary, "Keep continuous video for at least");
                ImGui::PopStyleVar();
                if(one_line)
                    ImGui::SameLine(0.0f, 16.0f);
                d.input("##hours", session.hours_text, sizeof(session.hours_text), ImGuiInputTextFlags_CharsDecimal, field_w);
                ImGui::SameLine();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(text_secondary, "hours");
                ImGui::PopStyleVar();
            }
            ImGui::EndDisabled();
            if(session.tried && do_motion_pruning && !session.hours_ok())
                d.error("Enter a whole number of hours, 1 or more.");
        };

        auto connection = [&](dialog_frame& d) {
            streams_section(d, streams, streams_cb, false);
            divider(d);
            rtsp_transport_options(d, rtsp_transport);
            if(!transport_status.empty())
                d.body(transport_status, status_warning_text);
            divider(d);
            ImGui::BeginDisabled(!can_sync_time);
            const bool sync = action(d, "Camera clock", "Sync Camera Time", can_sync_time
                ? "Set the camera's clock to this computer's. Its time zone is left as it is."
                : "Only a camera Revere found on the network (ONVIF) can have its clock set.");
            ImGui::EndDisabled();
            if(sync)
                sync_time_cb();
        };

        auto doorbell = [&](dialog_frame& d) {
            d.toggle("##doorbell", "Doorbell",
                "Pressing the button saves an event and notifies your phone, even if motion detection is off.",
                &is_doorbell);
            if(!is_doorbell)
                return;

            divider(d);
            d.label("How the doorbell tells Revere about a press");
            d.gap(2.0f);
            if(d.pill("##press_onvif", "ONVIF", press_source != "reolink_email", true))
                press_source = "onvif";
            if(d.pill("##press_email", "Reolink email", press_source == "reolink_email", false))
                press_source = "reolink_email";
            d.gap(2.0f);
            if(press_source != "reolink_email")
                d.body("The doorbell sends each press to Revere over ONVIF, right away. Most doorbells support this.");
            else
                d.body("For Reolink doorbells that don't send presses over ONVIF. The doorbell emails each press to this computer instead. The email stays on your network.");
            if(door.suggest_onvif && press_source == "reolink_email")
                d.body("This doorbell now sends presses over ONVIF too. Switch to ONVIF so rings arrive right away.", status_warning_text);

            if(press_source == "reolink_email")
            {
                divider(d);
                d.label("In the Reolink app");
                d.body("Open Settings > Email Alerts > Email Settings and enter:");
                if(!door.has_mail)
                    d.body(door.loaded ? "This computer doesn't have an address the doorbell can reach yet." : "Loading the email settings...");
                else
                {
                    // A field to type in, one to a line: its name as the Reolink app names it in a column of names, its
                    // value, and small Copy (and for the password Show or Hide, and New password) buttons at the right,
                    // under the value when they don't fit beside it.
                    d.gap(4.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
                    const auto rows = mail_setup_rows(door);
                    float widest = 0.0f;
                    for(auto& row : rows)
                        widest = std::max(widest, ImGui::CalcTextSize(row.name).x);
                    const float name_w = label_column_width(widest, d.width(), 200.0f);
                    const float spacing = 6.0f;
                    const float copy_w = main_window_detail::button_size("Copy").x;
                    const float show_w = std::max(main_window_detail::button_size("Show").x, main_window_detail::button_size("Hide").x);
                    const float new_w = main_window_detail::button_size("New password").x;
                    for(auto& row : rows)
                    {
                        ImGui::PushID(row.key);
                        const float x0 = ImGui::GetCursorScreenPos().x;
                        const float right = x0 + d.width();
                        const std::string shown = row.secret ? (door.reveal ? in_groups_of_4(row.value) : std::string(12, '*')) : row.value;
                        float buttons = row.copy ? copy_w : 0.0f;
                        if(row.secret)
                            buttons += show_w + spacing + new_w + spacing;
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextColored(text_secondary, "%s", row.name);
                        if(name_w > 0.0f)
                            ImGui::SameLine(0.0f, 0.0f);
                        ImGui::SetCursorScreenPos(ImVec2(x0 + name_w, ImGui::GetCursorScreenPos().y));
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextColored(text_primary, "%s", shown.c_str());
                        if(buttons > 0.0f)
                        {
                            // Beside the value when they fit, else on the next line at the right.
                            if(x0 + name_w + ImGui::CalcTextSize(shown.c_str()).x + 16.0f + buttons <= right)
                                ImGui::SameLine();
                            ImGui::SetCursorScreenPos(ImVec2(right - buttons, ImGui::GetCursorScreenPos().y));
                            if(row.secret)
                            {
                                if(main_window_detail::styled_button(door.reveal ? "Hide" : "Show"))
                                    door.reveal = !door.reveal;
                                ImGui::SameLine(0.0f, spacing);
                                ImGui::SetCursorScreenPos(ImVec2(right - copy_w - spacing - new_w, ImGui::GetCursorScreenPos().y));
                                if(main_window_detail::styled_button("New password"))
                                    door.confirm_new_password = true;
                                ImGui::SameLine(0.0f, spacing);
                            }
                            if(row.copy && main_window_detail::styled_button("Copy"))
                                ImGui::SetClipboardText(row.value.c_str());
                        }
                        ImGui::PopID();
                    }
                    ImGui::PopStyleVar();
                    if(door.confirm_new_password)
                    {
                        // Asked here first: the doorbell's presses stop until the new password is in its app.
                        d.gap(4.0f);
                        d.body("The doorbell will stop sending presses until you enter the new password in the Reolink app.", status_warning_text);
                        d.gap(2.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 5.0f));
                        if(main_window_detail::styled_button("Make a new password"))
                        {
                            door.confirm_new_password = false;
                            new_password_cb();
                        }
                        ImGui::SameLine(0.0f, 8.0f);
                        if(main_window_detail::styled_button("Cancel##new_password", button_kind::ghost))
                            door.confirm_new_password = false;
                        ImGui::PopStyleVar();
                    }
                    d.gap(4.0f);
                    d.body("Then set the email Alarm Schedule to Ring only. Otherwise the doorbell also emails when it sees motion.");
                }
                d.gap(4.0f);
                d.body("The doorbell sends its email about 15 seconds after the press, so the ring arrives late.");
                d.body("The doorbell sends at most one email about every 10 minutes, and has no setting to change that. A second press in that time doesn't reach Revere.");
                for(auto& line : mail_status_lines(door, press_source))
                    d.body(line);
            }

            divider(d);
            if(door.test_open)
            {
                const int64_t left = std::max<int64_t>(0, (door.test_ends_ms - (int64_t)std::time(nullptr) * 1000 + 999) / 1000);
                d.label("Test it");
                d.body("Press the doorbell now. " + std::to_string(left) + (left == 1 ? " second left." : " seconds left."));
            }
            else if(action(d, "Test it", "Start test", "Click Start test, then press the doorbell within a minute. Revere shows how the press arrived and how long it took. Your phone won't ring during the test."))
                press_test_cb();
            const auto lines = press_test_lines(door);
            if(!lines.empty())
                d.gap(2.0f);
            for(auto& line : lines)
                d.body(line, text_primary);
            const auto verdict = press_test_verdict(door, press_source);
            if(!verdict.empty())
                d.body(verdict);
            if(door.test_any && !door.test_open && door.saved_source != press_source)
                d.body("The test checks the saved choice. Save first to test the other one.");
        };

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        section("Recording", recording);
        section("Detection", detection);
        section("Streams and connection", connection);
        section("Doorbell", doorbell);
    }
    ImGui::PopFont();
    session.nav.end_frame();
    session.nav.scroll_y = ImGui::GetScrollY();
    session.nav.scroll_max = ImGui::GetScrollMaxY();
    ImGui::EndChild();

    // Fixed at the bottom while the sections scroll, at the right of the sections' column. Save waits for a change,
    // and for the camera to finish being asked about its streams (that answer is what Save would write).
    camera_properties_result result = camera_properties_result::none;
    ImGui::Separator();
    {
        dialog_frame d(dialog_frame::embedded, content_w);
        const int a = d.actions({{"Cancel"}, {"Save", button_kind::primary, dirty && !checking_streams}});
        if(a == 0)
            result = camera_properties_result::cancel;
        else if(a == 1)
        {
            session.tried = true;
            // The hours only apply with pruning on; leave a bad value alone otherwise.
            if(session.hours_ok() || !do_motion_pruning)
            {
                if(session.hours_ok())
                    min_continuous_retention_hours = session.hours_text;
                result = camera_properties_result::save;
            }
        }
    }
    return result;
}

}

#endif
