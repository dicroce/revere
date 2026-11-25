#ifndef __revere_camera_properties_pane_h
#define __revere_camera_properties_pane_h

#include "camera_properties_state.h"
#include "dialogs.h"

// A camera's properties, edited in the camera's pane (the right-hand detail pane) rather than in a dialog: the
// sections sit side by side, as many columns as fit, and scroll; Cancel and Save stay at the bottom right.
//
// Sections: Recording (where the recording lives and the two storage operations), Detection (motion, what finds it,
// pruning), Streams and connection (the smaller stream, TCP or UDP, the camera's clock) and Doorbell (whether the
// device is one). Another section is one more entry in `sections` below; room is left in Recording for "Record from"
// and in Doorbell for the press source and press test.

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
// empty, says the camera is on UDP because it refused TCP; record_from is "main" or "sub", the stream it records from,
// and record_from_available says the camera's smaller stream works as saved (the choice is off until it does, unless
// it is already set); streams is the Streams state (see streams_state) and
// streams_cb(op, token) is called for a click in it: op "off" (use the recording stream), "use" (the smaller stream
// with that profile token), "find" (look again) or "analysis" (token "1" or "0"). can_sync_time: the camera was found
// by ONVIF discovery, so its clock can be set. sync_time_cb, move_storage_cb and change_retention_cb open the small
// dialogs for those. On Save with a valid form, min_continuous_retention_hours holds the hours field.
template<typename STREAMS_CB, typename SYNC_TIME_CB, typename MOVE_STORAGE_CB, typename CHANGE_RETENTION_CB>
camera_properties_result camera_properties_pane(
    camera_properties_session& session,
    bool& is_doorbell,
    bool& do_motion_detection,
    bool& do_motion_pruning,
    std::string& min_continuous_retention_hours,
    std::string& motion_tier,
    const std::string& tier_status,
    std::string& rtsp_transport,
    const std::string& transport_status,
    std::string& record_from,
    bool record_from_available,
    const std::string& record_file_path,
    streams_state& streams,
    bool can_sync_time,
    STREAMS_CB streams_cb,
    SYNC_TIME_CB sync_time_cb,
    MOVE_STORAGE_CB move_storage_cb,
    CHANGE_RETENTION_CB change_retention_cb
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
    const bool dirty = session.dirty(is_doorbell, do_motion_detection, do_motion_pruning, motion_tier, rtsp_transport, record_from, streams_changed);

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
            // The stream recorded. The smaller stream can be chosen once it works (and stays chosen if it goes away).
            d.gap(6.0f);
            d.label("Record from");
            d.gap(2.0f);
            if(d.pill("##record_main", "Main stream", record_from != "sub", true))
                record_from = "main";
            // Already the saved choice when the pane opened (the stream may just be away): it stays choosable, so flipping to Main
            // and back needs no Cancel.
            const bool sub_choosable = record_from_available || session.record_from == "sub";
            ImGui::BeginDisabled(!sub_choosable);
            if(d.pill("##record_sub", "Smaller stream", record_from == "sub", false))
                record_from = "sub";
            ImGui::EndDisabled();
            d.gap(2.0f);
            d.body(record_from_description(record_from));
            const auto status = record_from_status(record_from, record_from_available);
            if(!status.empty())
            {
                d.gap(2.0f);
                d.body(status, record_from == "sub" ? status_warning_text : text_secondary);
            }
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
            // The press source and the press test go here.
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
