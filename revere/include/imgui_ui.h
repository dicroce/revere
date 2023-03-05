
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
#include <map>
#include <functional>
#include <mutex>
#include <string>
#include <cstdint>
#include "assignment_state.h"
#include "rtsp_source_camera_config.h"
#include "gl_utils.h"
#include "r_ui_utils/font_catalog.h"
#include "r_ui_utils/utils.h"

namespace revere
{

template<typename OK_CB>
void error_modal(
    ImGuiContext* GImGui,
    const std::string& name,
    const std::string& msg,
    OK_CB ok_cb
)
{
    if (ImGui::BeginPopupModal(name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImVec2 pos_before = window->DC.CursorPos;

        ImGui::Text("%s", msg.c_str());

        if(ImGui::Button("Ok"))
        {
            ImGui::CloseCurrentPopup();
            ok_cb();
        }

        ImGui::EndPopup();
    }
}

template<typename EXIT_CB, typename MINIMIZE_CB, typename ADD_RTSP_SOURCE_CAMERA_CB, typename LAUNCH_VISION_CB>
uint16_t main_menu(EXIT_CB exit_cb, MINIMIZE_CB minimize_cb, ADD_RTSP_SOURCE_CAMERA_CB add_rtsp_source_camera_cb, LAUNCH_VISION_CB launch_vision_cb)
{
    ImGui::BeginMainMenuBar();
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Add RTSP Source Camera"))
            add_rtsp_source_camera_cb();
        if (ImGui::MenuItem("Minimize to system tray"))
            minimize_cb();
        if (ImGui::MenuItem("Launch Vision"))
            launch_vision_cb();
        if (ImGui::MenuItem("Exit"))
            exit_cb();

        ImGui::EndMenu();
    }
    uint16_t h = (uint16_t)ImGui::GetWindowHeight();
    ImGui::EndMainMenuBar();
    return h;
}

template<typename MAIN_CB>
void main_layout(
    uint16_t client_top,
    uint16_t window_width,
    uint16_t window_height,
    MAIN_CB main_cb,
    r_utils::r_nullable<std::string> status_text
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
    ImGui::PushFont(r_ui_utils::fonts["18.00"].roboto_regular);
    ImGui::SetNextWindowPos(ImVec2(0, (float)(window_height - line_height)));
    ImGui::SetNextWindowSize(ImVec2(window_width, line_height));
    ImGui::Begin("##status", &open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    if(!status_text.is_null())
    {
        ImGui::SetCursorPos(ImVec2(10, 5));
        ImGui::Text("%s",status_text.value().c_str());
    }
    ImGui::End();
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
    uint16_t panel_width = window_width / 3;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(panel_width, window_height));
    ImGui::Begin(first_label.c_str());
    first_cb(panel_width);
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2((float)(x + panel_width), (float)y));
    ImGui::SetNextWindowSize(ImVec2(panel_width, window_height));
    ImGui::Begin(second_label.c_str());
    second_cb(panel_width);
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(x + ((float)panel_width*2), y));
    ImGui::SetNextWindowSize(ImVec2(panel_width, window_height));
    ImGui::Begin(third_label.c_str());
    third_cb(panel_width);
    ImGui::End();
}

template<typename OK_CB, typename CANCEL_CB>
void rtsp_credentials_modal(
    ImGuiContext* GImGui,
    const std::string& name,
    r_utils::r_nullable<std::string>& u,
    r_utils::r_nullable<std::string>& p,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    if (ImGui::BeginPopupModal(name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImVec2 pos_before = window->DC.CursorPos;

        ImGui::Text("Please provide the credentials for the Onvif user you wish\nto connect to this camera as:");

        char username[64] = {0};
        if(!u.is_null())
            r_ui_utils::copy_s(username, 64, u.value());
        if(ImGui::InputText("Username", username, 64))
            u.set_value(std::string(username));

        char password[64] = {0};
        if(!p.is_null())
            r_ui_utils::copy_s(password, 64, p.value());
        if(ImGui::InputText("Password", password, 64, ImGuiInputTextFlags_Password))
            p.set_value(std::string(password));

        if(ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
            cancel_cb();
        }

        ImGui::SameLine();

        if(ImGui::Button("Ok"))
        {
            ImGui::CloseCurrentPopup();
            ok_cb();
        }

        ImGui::EndPopup();
    }
}

struct sidebar_list_ui_item
{
    std::string label;
    std::string sub_label;
    std::string camera_id;
};

template<typename BUTTON_CLICK_CB, typename ITEM_CLICK_CB, typename FORGET_BUTTON_CLICK_CB, typename PROPERTIES_CLICK_CB>
void sidebar_list(
    ImGuiContext* GImGui,
    revere::assignment_state& as,
    int selected,
    uint16_t width,
    std::vector<sidebar_list_ui_item>& labels,
    const std::string& button_label,
    BUTTON_CLICK_CB button_click_cb,
    ITEM_CLICK_CB item_click_cb,
    bool show_forget_button,
    FORGET_BUTTON_CLICK_CB forget_button_click_cb,
    bool show_properties_button,
    PROPERTIES_CLICK_CB properties_click_cb
)
{
    auto pos = ImGui::GetCursorPos();

    float largest_label = 0;
    for(int i = 0; i < labels.size(); ++i)
    {
        auto label_size = ImGui::CalcTextSize(labels[i].label.c_str());
        auto sub_label_size = ImGui::CalcTextSize(labels[i].sub_label.c_str());

        if(label_size.x > largest_label)
            largest_label = label_size.x;
        if(sub_label_size.x > largest_label)
            largest_label = sub_label_size.x;
    }

    // selectable list
    for(int i = 0; i < labels.size(); ++i)
    {
        ImGui::PushID(i);

        auto name = r_utils::r_string_utils::format("##Object %d", i);

        // Note: Though the button is actually inside the selectable it has to come first because otherwise
        // the Selectable will steal the click event.
        const int button_width = 100;
        const int button_height = 35;
        ImGui::SetCursorPos(ImVec2(pos.x+10, pos.y + 55));
        if(ImGui::Button(button_label.c_str(), ImVec2(button_width, button_height)))
        {
            item_click_cb(i);
            button_click_cb(i);
        }

        if(show_forget_button)
        {
            ImGui::SameLine();
            if(ImGui::Button("Forget"))
                forget_button_click_cb(i);
        }

        if(show_properties_button)
        {
            ImGui::SameLine();
            if(ImGui::Button("Properties"))
                properties_click_cb(i);
        }

        ImGui::SetCursorPos(ImVec2(pos.x, pos.y));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(20, 52, 77, 200));                     // modify a style color. always use this if you modify the style after NewFrame().
        ImGui::PushStyleColor(ImGuiCol_NavHighlight, IM_COL32(255, 0, 0, 200));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(25, 47, 63, 200));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(49, 55, 62, 200));
        if (ImGui::Selectable(name.c_str(), i == selected, 0, ImVec2((float)width, 100))) {
            item_click_cb(i);
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();

        // GetCursorScreenPos() here allows the IMDrawList to scroll when the containing
        // window scrolls.
        auto cursor_pos = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddLine(ImVec2(cursor_pos.x, cursor_pos.y), ImVec2(cursor_pos.x + width, cursor_pos.y), IM_COL32(40, 40, 40, 200));

        // Center button in region to right of text
        std::string label = labels[i].label;
        std::string sub_label = labels[i].sub_label;

        ImGui::PushFont(r_ui_utils::fonts["24.00"].roboto_bold);
        ImGui::SetCursorPos(ImVec2(pos.x+10, pos.y+5));
        ImGui::Text("%s", label.c_str());
        ImGui::PopFont();


        ImGui::PushFont(r_ui_utils::fonts["22.00"].roboto_regular);
        ImGui::SetCursorPos(ImVec2(pos.x+10, pos.y+28));
        ImGui::Text("%s", sub_label.c_str());
        ImGui::PopFont();

        pos.y += 110;

        ImGui::PopID();
    }
}

template<typename OK_CB, typename CANCEL_CB>
void friendly_name_modal(
    ImGuiContext* GImGui,
    const std::string& name,
    revere::assignment_state& as,
    std::string& fn,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    ImGui::SetNextWindowSize(ImVec2(700, 360));
    if (ImGui::BeginPopupModal(name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImVec2 pos_before = window->DC.CursorPos;

        ImGui::SetCursorPos(ImVec2(350 - (320/2), 40));
        ImGui::Image((void*)(intptr_t)as.key_frame_texture, ImVec2(320, 240));

        char friendly_name[64] = {0};
        r_ui_utils::copy_s(friendly_name, 64, fn);
        if(ImGui::InputText("Camera Friendly Name", friendly_name, 64))
            fn = std::string(friendly_name);

        if(ImGui::Button("Cancel", ImVec2(80, 30)))
        {
            ImGui::CloseCurrentPopup();
            cancel_cb();
        }

        ImGui::SameLine();

        if(ImGui::Button("Ok", ImVec2(80, 30)))
        {
            ImGui::CloseCurrentPopup();
            ok_cb();
        }

        ImGui::EndPopup();
    }
}

template<typename CANCEL_CB>
void please_wait_modal(
    ImGuiContext* GImGui,
    const std::string& name,
    CANCEL_CB cancel_cb
)
{
    ImGui::SetNextWindowSize(ImVec2(600, 200));

    if (ImGui::BeginPopupModal(name.c_str(), NULL, ImGuiWindowFlags_NoTitleBar))
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImVec2 pos_before = window->DC.CursorPos;

        auto msg_width = ImGui::CalcTextSize("Please wait while we communicate with your camera...");
        ImGui::SetCursorPos(ImVec2(300-(msg_width.x/2), 50-15));
        ImGui::Text("Please wait while we communicate with your camera...");

        ImGui::SetCursorPos(ImVec2(300-35, 150-15));
        if(ImGui::Button("Cancel",ImVec2(70,30)))
        {
            ImGui::CloseCurrentPopup();
            cancel_cb();
        }

        ImGui::EndPopup();
    }
}

template<typename OK_CB, typename CANCEL_CB>
void motion_detection_modal(
    ImGuiContext* GImGui,
    const std::string& name,
    bool& do_motion_detection,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    if (ImGui::BeginPopupModal(name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImVec2 pos_before = window->DC.CursorPos;

        int e = (do_motion_detection)?0:1;
        ImGui::RadioButton("yes", &e, 0); ImGui::SameLine();
        ImGui::RadioButton("no", &e, 1); ImGui::SameLine();
        do_motion_detection = (e == 0);

        if(ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
            cancel_cb();
        }

        ImGui::SameLine();

        if(ImGui::Button("Ok"))
        {
            ImGui::CloseCurrentPopup();
            ok_cb();
        }

        ImGui::EndPopup();
    }
}

template<typename NEW_CB, typename EXISTING_CB, typename CANCEL_CB>
void new_or_existing_modal(
    ImGuiContext* GImGui,
    const std::string& name,
    NEW_CB new_cb,
    EXISTING_CB existing_cb,
    CANCEL_CB cancel_cb
)
{
    if (ImGui::BeginPopupModal(name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImVec2 pos_before = window->DC.CursorPos;

        if(ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
            cancel_cb();
        }

        ImGui::SameLine();

        if(ImGui::Button("New Storage File"))
        {
            ImGui::CloseCurrentPopup();
            new_cb();
        }

        ImGui::SameLine();

        if(ImGui::Button("Existing Storage File"))
        {
            ImGui::CloseCurrentPopup();
            existing_cb();
        }

        ImGui::EndPopup();
    }
}

template<typename OK_CB, typename CANCEL_CB>
void new_file_name_modal(
    ImGuiContext* GImGui,
    const std::string& name,
    std::string& file_name,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    if (ImGui::BeginPopupModal(name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImVec2 pos_before = window->DC.CursorPos;

        char new_file_name[64] = {0};
        r_ui_utils::copy_s(new_file_name, 64, file_name);
        if(ImGui::InputText("New File Name", new_file_name, 64))
            file_name = std::string(new_file_name);

        if(ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
            cancel_cb();
        }

        ImGui::SameLine();

        if(ImGui::Button("Ok"))
        {
            ImGui::CloseCurrentPopup();
            ok_cb();
        }

        ImGui::EndPopup();
    }
}

template<typename OK_CB, typename CANCEL_CB>
void retention_modal(
    ImGuiContext* GImGui,
    const std::string& name,
    revere::assignment_state& as,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    if (ImGui::BeginPopupModal(name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImVec2 pos_before = window->DC.CursorPos;

        ImGui::Text("%s",(as.camera_friendly_name + " at " + std::to_string((as.byte_rate * 8) / 1024) + " kbps").c_str());

        char continuous_retention_days_buffer[64] = {0};
        r_ui_utils::copy_s(continuous_retention_days_buffer, 64, std::to_string(as.continuous_retention_days));
        if(ImGui::InputText("Continous Retention Days", continuous_retention_days_buffer, 64))
        {
            auto s_continuous_retention_days = std::string(continuous_retention_days_buffer);
            as.continuous_retention_days = std::stoi((!s_continuous_retention_days.empty())?s_continuous_retention_days:"0");
        }

        char days_motion_retention_buffer[64] = {0};
        r_ui_utils::copy_s(days_motion_retention_buffer, 64, std::to_string(as.motion_retention_days));
        if(ImGui::InputText("Motion Retention Days", days_motion_retention_buffer, 64))
        {
            auto s_motion_retention_days = std::string(days_motion_retention_buffer);
            as.motion_retention_days = std::stoi((!s_motion_retention_days.empty())?s_motion_retention_days:"0");
        }

        char motion_percentage_estimate_buffer[64] = {0};
        r_ui_utils::copy_s(motion_percentage_estimate_buffer, 64, std::to_string(as.motion_percentage_estimate));
        if(ImGui::InputText("Motion Percentage Estimate", motion_percentage_estimate_buffer, 64))
        {
            auto s_motion_percentage_estimate = std::string(motion_percentage_estimate_buffer);
            as.motion_percentage_estimate = std::stoi((!s_motion_percentage_estimate.empty())?s_motion_percentage_estimate:"0");
        }

        auto continuous_sz_info = r_storage::r_storage_file::required_file_size_for_retention_hours((as.continuous_retention_days*24), as.byte_rate);

        auto motion_sz_info = r_storage::r_storage_file::required_file_size_for_retention_hours((as.motion_retention_days*24), as.byte_rate);

        double motionPercentage = ((double)as.motion_percentage_estimate) / 100.0;

        int64_t n_blocks = (int64_t)(continuous_sz_info.first + (motionPercentage*(double)motion_sz_info.first));

        as.num_storage_file_blocks.set_value(n_blocks);
        as.storage_file_block_size.set_value(continuous_sz_info.second);

        auto human_readable_size = r_storage::r_storage_file::human_readable_file_size((double)(as.num_storage_file_blocks.value() * as.storage_file_block_size.value()));

        ImGui::Text("Days Retention: %s", human_readable_size.c_str());

        if(ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
            cancel_cb();
        }

        ImGui::SameLine();

        if(ImGui::Button("Ok"))
        {
            ImGui::CloseCurrentPopup();
            ok_cb();
        }

        ImGui::EndPopup();
    }
}

template<typename OK_CB, typename CANCEL_CB>
void minimize_to_tray_modal(
    ImGuiContext* GImGui,
    const std::string& name,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    if (ImGui::BeginPopupModal(name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImVec2 pos_before = window->DC.CursorPos;

        ImGui::Text("Revere will now be minimized to the system tray. You can restore it by clicking the tray icon.");

        if(ImGui::Button("Cancel"))
        {
            cancel_cb();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if(ImGui::Button("Ok"))
        {
            ok_cb();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

template<typename OK_CB, typename CANCEL_CB>
void configure_rtsp_source_camera_modal(
    ImGuiContext* GImGui,
    const std::string& name,
    rtsp_source_camera_config& rscc,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    if (ImGui::BeginPopupModal(name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImVec2 pos_before = window->DC.CursorPos;

        char camera_model_buffer[64] = {0};
        r_ui_utils::copy_s(camera_model_buffer, 64, rscc.camera_name);

        if(ImGui::InputText("Camera Model", camera_model_buffer, 64))
            rscc.camera_name = std::string(camera_model_buffer);

        char ipv4_buffer[64] = {0};
        r_ui_utils::copy_s(ipv4_buffer, 64, rscc.ipv4);

        if(ImGui::InputText("IPv4", ipv4_buffer, 64))
            rscc.ipv4 = std::string(ipv4_buffer);

        char rtsp_url_buffer[64] = {0};
        r_ui_utils::copy_s(rtsp_url_buffer, 64, rscc.rtsp_url);

        if(ImGui::InputText("RTSP URL", rtsp_url_buffer, 64))
            rscc.rtsp_url = std::string(rtsp_url_buffer);

        char rtsp_username_buffer[64] = {0};
        r_ui_utils::copy_s(rtsp_username_buffer, 64, rscc.rtsp_username);

        if(ImGui::InputText("RTSP Username", rtsp_username_buffer, 64))
            rscc.rtsp_username = std::string(rtsp_username_buffer);

        char rtsp_password_buffer[64] = {0};
        r_ui_utils::copy_s(rtsp_password_buffer, 64, rscc.rtsp_password);

        if(ImGui::InputText("RTSP Password", rtsp_password_buffer, 64))
            rscc.rtsp_password = std::string(rtsp_password_buffer);

        if(ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
            cancel_cb();
        }

        ImGui::SameLine();

        if(ImGui::Button("Ok"))
        {
            ImGui::CloseCurrentPopup();
            ok_cb();
        }

        ImGui::EndPopup();
    }
}

template<typename OK_CB, typename CANCEL_CB>
void camera_properties_modal(
    ImGuiContext* GImGui,
    const std::string& name,
    bool& do_motion_pruning,
    std::string& min_continuous_retention_hours,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    if (ImGui::BeginPopupModal(name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImVec2 pos_before = window->DC.CursorPos;

        ImGui::Checkbox("Prune Still Video", &do_motion_pruning);

        char retention_hours[64] = {0};
        r_ui_utils::copy_s(retention_hours, 64, min_continuous_retention_hours);
        if(ImGui::InputText("Minimum continuous retention hours", retention_hours, 64))
            min_continuous_retention_hours = std::string(retention_hours);

        if(ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
            cancel_cb();
        }

        ImGui::SameLine();

        if(ImGui::Button("Ok"))
        {
            ImGui::CloseCurrentPopup();
            ok_cb();
        }

        ImGui::EndPopup();
    }
}

}

#endif
