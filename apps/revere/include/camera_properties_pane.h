#ifndef __revere_camera_properties_pane_h
#define __revere_camera_properties_pane_h

#include "camera_properties_state.h"
#include "dialogs.h"

// A camera's properties, edited in the camera's pane (the right-hand detail pane) rather than in a dialog: the
// sections sit side by side, as many columns as fit, and scroll; Cancel and Save stay at the bottom right.
//
// Sections: Recording (where the recording lives and the two storage operations), Detection (motion, what finds it,
// pruning), Streams and connection (the smaller stream, TCP or UDP, the camera's clock) and Doorbell (whether the
// device is one, how it reports a press, what to type in a Reolink doorbell's app, and "Press the doorbell now").
// Another section is one more entry in `sections` below; room is left in Recording for "Record from".

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
// email setup (doorbell_view), and press_test_cb() starts "Press the doorbell now".
template<typename STREAMS_CB, typename SYNC_TIME_CB, typename MOVE_STORAGE_CB, typename CHANGE_RETENTION_CB, typename PRESS_TEST_CB>
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
    PRESS_TEST_CB press_test_cb
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

    const float footer_h = 62.0f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 4.0f));
    ImGui::BeginChild("##properties_sections", ImVec2(0.0f, std::max(80.0f, avail.y - footer_h)), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    {
        const float w = ImGui::GetContentRegionAvail().x;
        // Columns of about 340 px or more; the sections wrap into rows when there are more than columns.
        const int columns = w >= 1060.0f ? 3 : (w >= 700.0f ? 2 : 1);

        // A button with a line saying what it does under it.
        auto row_action = [&](dialog_frame& d, const char* label, const char* description) {
            d.gap(2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 4.0f));
            const bool clicked = ImGui::Button(label);
            ImGui::PopStyleVar();
            d.body(description);
            return clicked;
        };

        auto recording = [&](dialog_frame& d) {
            d.section("Recording");
            if(!record_file_path.empty())
            {
                d.label("Recording file");
                if(d.path(record_file_path, "Copy path"))
                    ImGui::SetClipboardText(record_file_path.c_str());
            }
            if(row_action(d, "Move storage", "Move this camera's recording files to another folder or disk."))
                move_storage_cb();
            if(row_action(d, "Change retention", "Resize the recording file to hold more or fewer days of video. Clears this camera's existing recordings."))
                change_retention_cb();
            // "Record from" (the stream recorded) goes here.
        };

        auto detection = [&](dialog_frame& d) {
            d.section("Detection");
            d.toggle("##motion", "Motion detection",
                "Finds movement in this camera's video and marks it as motion events you can search and review.",
                &do_motion_detection);

            ImGui::BeginDisabled(!do_motion_detection);
            d.gap(4.0f);
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
            d.input("##hours", session.hours_text, sizeof(session.hours_text), ImGuiInputTextFlags_CharsDecimal, 80.0f);
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(text_secondary, "hours");
            ImGui::EndDisabled();
            if(session.tried && do_motion_pruning && !session.hours_ok())
                d.error("Enter a whole number of hours, 1 or more.");
        };

        auto connection = [&](dialog_frame& d) {
            d.section("Streams and connection");
            streams_section(d, streams, streams_cb, false);
            d.gap(6.0f);
            rtsp_transport_options(d, rtsp_transport);
            if(!transport_status.empty())
            {
                d.gap(2.0f);
                d.body(transport_status, status_warning_text);
            }
            d.gap(6.0f);
            d.label("Camera clock");
            ImGui::BeginDisabled(!can_sync_time);
            const bool sync = row_action(d, "Sync Camera Time", can_sync_time
                ? "Set the camera's clock to this computer's. Its time zone is left as it is."
                : "Only a camera Revere found on the network (ONVIF) can have its clock set.");
            ImGui::EndDisabled();
            if(sync)
                sync_time_cb();
        };

        auto doorbell = [&](dialog_frame& d) {
            d.section("Doorbell");
            d.toggle("##doorbell", "Doorbell",
                "This is a doorbell: a press of its button rings, as an event and a notification, whatever the detection settings.",
                &is_doorbell);
            if(!is_doorbell)
                return;

            d.gap(6.0f);
            d.label("How it tells Revere it was pressed");
            d.gap(2.0f);
            if(d.pill("##press_onvif", "ONVIF", press_source != "reolink_email", true))
                press_source = "onvif";
            if(d.pill("##press_email", "Reolink email", press_source == "reolink_email", false))
                press_source = "reolink_email";
            d.gap(2.0f);
            if(press_source != "reolink_email")
                d.body("At once, over ONVIF. Most doorbells report a press this way.");
            else
                d.body("For a Reolink doorbell that doesn't report presses over ONVIF: it emails this computer instead, and Revere reads the email. Only this computer, never the internet.");
            if(door.suggest_onvif && press_source == "reolink_email")
            {
                d.gap(2.0f);
                d.body("This doorbell now reports presses over ONVIF too. Switch to ONVIF for rings at once.", status_warning_text);
            }

            if(press_source == "reolink_email")
            {
                d.gap(6.0f);
                d.label("In the Reolink app");
                d.body("Settings > Email Alerts > Email Settings, then enter:");
                if(!door.has_mail)
                    d.body(door.loaded ? "This computer has no network address toward the doorbell yet." : "Reading the doorbell's settings...");
                else
                {
                    // A field to type in, on one line: its name, its value, and small Copy (and for the password Show or
                    // Hide) buttons at the right.
                    const auto& style = ImGui::GetStyle();
                    const float name_w = ImGui::CalcTextSize("Recipient address").x + 12.0f;
                    const float copy_w = ImGui::CalcTextSize("Copy").x + style.FramePadding.x * 2.0f;
                    const float show_w = std::max(ImGui::CalcTextSize("Show").x, ImGui::CalcTextSize("Hide").x) + style.FramePadding.x * 2.0f;
                    auto field = [&](const char* id, const char* name, const std::string& value, bool copy, bool secret) {
                        ImGui::PushID(id);
                        d.gap(3.0f);
                        // Screen positions: SameLine's offsets are from the column's start, which a table moves.
                        const float x0 = ImGui::GetCursorScreenPos().x;
                        const float right = x0 + d.width();
                        ImGui::TextColored(text_secondary, "%s", name);
                        ImGui::SameLine();
                        ImGui::SetCursorScreenPos(ImVec2(x0 + name_w, ImGui::GetCursorScreenPos().y));
                        const std::string shown = secret && !door.reveal ? std::string(12, '*') : value;
                        ImGui::TextColored(text_primary, "%s", shown.c_str());
                        float buttons = copy ? copy_w : 0.0f;
                        if(secret)
                            buttons += show_w + 4.0f;
                        if(buttons > 0.0f)
                        {
                            // Beside the value when they fit, else on the next line at the right.
                            const bool fits = x0 + name_w + ImGui::CalcTextSize(shown.c_str()).x + 8.0f + buttons <= right;
                            if(fits)
                                ImGui::SameLine();
                            ImGui::SetCursorScreenPos(ImVec2(right - buttons, ImGui::GetCursorScreenPos().y));
                            if(secret)
                            {
                                if(ImGui::Button(door.reveal ? "Hide" : "Show", ImVec2(show_w, 0.0f)))
                                    door.reveal = !door.reveal;
                                ImGui::SameLine(0.0f, 4.0f);
                            }
                            if(copy && ImGui::Button("Copy", ImVec2(copy_w, 0.0f)))
                                ImGui::SetClipboardText(value.c_str());
                        }
                        ImGui::PopID();
                    };
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 1.0f));
                    field("server", "SMTP server", door.server, true, false);
                    field("port", "SMTP port", std::to_string(door.port), true, false);
                    field("tls", "SSL or TLS", "On", false, false);
                    field("user", "User name", door.user, true, false);
                    field("password", "Password", door.password, true, true);
                    field("sender", "Sender address", "Anything", false, false);
                    field("recipient", "Recipient address", door.recipient, true, false);
                    ImGui::PopStyleVar();
                    d.gap(4.0f);
                    d.body("Then set the email Alarm Schedule to Ring only, so motion doesn't send email.");
                }
                d.gap(4.0f);
                d.body("A ring arrives about 15 seconds after the press: Reolink sends the email late.");
                d.body("Reolink sends at most one email about every 10 minutes, and the doorbell has no setting to change that. Another press within that time isn't reported.");
                for(auto& line : mail_status_lines(door, press_source))
                {
                    d.gap(2.0f);
                    d.body(line);
                }
            }

            d.gap(6.0f);
            d.label("Check it");
            if(door.test_open)
            {
                const int64_t left = std::max<int64_t>(0, (door.test_ends_ms - (int64_t)std::time(nullptr) * 1000 + 999) / 1000);
                d.body("Press the doorbell's button now. Waiting " + std::to_string(left) + " s for its reports; this press doesn't ring the phones.");
            }
            else if(row_action(d, "Press the doorbell now", "For a minute, Revere notes which of the doorbell's reports arrive, and how late. That press doesn't ring the phones."))
                press_test_cb();
            for(auto& line : press_test_lines(door))
                d.body(line, text_primary);
            const auto verdict = press_test_verdict(door, press_source);
            if(!verdict.empty())
            {
                d.gap(2.0f);
                d.body(verdict);
            }
            if(door.test_any && !door.test_open && door.saved_source != press_source)
                d.body("The test uses the saved choice's sources: save first to test the other one.");
        };

        std::vector<std::function<void(dialog_frame&)>> sections {recording, detection, connection, doorbell};

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(14.0f, 0.0f));
        if(ImGui::BeginTable("##properties_columns", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
            for(size_t i = 0; i < sections.size(); ++i)
            {
                ImGui::TableNextColumn();
                ImGui::PushID((int)i);
                {
                    dialog_frame d(dialog_frame::embedded, std::max(180.0f, ImGui::GetContentRegionAvail().x));
                    sections[i](d);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    // Fixed at the bottom right while the sections scroll. Save waits for a change, and for the camera to finish
    // being asked about its streams (that answer is what Save would write).
    camera_properties_result result = camera_properties_result::none;
    ImGui::Separator();
    {
        dialog_frame d(dialog_frame::embedded, std::max(0.0f, ImGui::GetContentRegionAvail().x));
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
