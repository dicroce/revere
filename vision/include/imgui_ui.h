
#ifndef __vision_imgui_ui_h
#define __vision_imgui_ui_h

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/ImGuiFileDialog.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_ui_utils/font_catalog.h"
#include "r_ui_utils/utils.h"
#include <map>
#include <functional>
#include <mutex>
#include <string>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include "gl_utils.h"
#include "configure_state.h"
#include "layouts.h"
#include "control_bar.h"

namespace vision
{

struct one_by_one_state
{
    control_bar_state cbs;
};

struct main_client_state
{
    one_by_one_state obos;
};

template<typename EXIT_CB, typename SETTINGS_CB, typename ONE_BY_ONE_CB, typename TWO_BY_TWO_CB, typename FOUR_BY_FOUR_CB>
uint16_t main_menu(EXIT_CB exit_cb, SETTINGS_CB settings_cb, ONE_BY_ONE_CB one_by_one_cb, TWO_BY_TWO_CB two_by_two_cb, FOUR_BY_FOUR_CB four_by_four_cb)
{
    ImGui::BeginMainMenuBar();
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Configure"))
            settings_cb();
        if (ImGui::MenuItem("Exit"))
            exit_cb();

        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem("1x1"))
            one_by_one_cb();
        if (ImGui::MenuItem("2x2"))
            two_by_two_cb();
        if (ImGui::MenuItem("4x4"))
            four_by_four_cb();

        ImGui::EndMenu();
    }
    uint16_t h = (uint16_t)ImGui::GetWindowHeight();
    ImGui::EndMainMenuBar();
    return h;
}

template<typename OK_CB, typename CANCEL_CB>
void configure_modal(
    ImGuiContext* GImGui,
    const std::string& name,
    std::string& ip_storage,
    vision::configure_state& cs,
    OK_CB ok_cb,
    CANCEL_CB cancel_cb
)
{
    ImGui::SetNextWindowSize(ImVec2(500, 150));
    if (ImGui::BeginPopupModal(name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImVec2 pos_before = window->DC.CursorPos;

        ImGui::SetCursorPos(ImVec2(250 - (430/2), 40));

        char revere_ipv4[64] = {0};
        if(!cs.revere_ipv4.is_null())
            r_ui_utils::copy_s(revere_ipv4, 64, ip_storage);
        
        if(ImGui::InputText("Revere IPV4 Address", revere_ipv4, 64))
            ip_storage = std::string(revere_ipv4);

        ImGui::SetCursorPos(ImVec2(340, 90));

        if(ImGui::Button("Cancel", ImVec2(60, 30)))
        {
            ImGui::CloseCurrentPopup();
            cancel_cb();
        }

        ImGui::SameLine();

        if(ImGui::Button("Ok", ImVec2(60, 30)))
        {
            ImGui::CloseCurrentPopup();
            ok_cb();
        }

        ImGui::EndPopup();
    }
}

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

template<typename SIDEBAR_CB, typename MAIN_CB>
void main_layout(
    uint16_t client_top,
    uint16_t window_width,
    uint16_t window_height,
    const std::string& sidebar_label,
    SIDEBAR_CB sidebar_cb,
    const std::string& main_label,
    MAIN_CB main_cb,
    r_utils::r_nullable<std::string> status_text
)
{
    bool open = true;

    uint16_t panel_width = window_width / 6;
    ImGui::SetNextWindowPos(ImVec2(0, client_top));
    ImGui::SetNextWindowSize(ImVec2(panel_width, window_height - (ImGui::GetTextLineHeightWithSpacing()*2)));
    ImGui::Begin(sidebar_label.c_str(), &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    sidebar_cb(panel_width);
    ImGui::End();

    uint16_t main_width = window_width - panel_width;
    uint16_t main_height = window_height - ((uint16_t)(ImGui::GetTextLineHeightWithSpacing()*2));
    ImGui::SetNextWindowPos(ImVec2(panel_width, client_top));
    ImGui::SetNextWindowSize(ImVec2(main_width, main_height));
    ImGui::Begin("##main", &open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    main_cb(main_width, main_height);
    ImGui::End();


    open = true;
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

template<typename ONE_CB>
void one_by_one(
    uint16_t left,
    uint16_t top,
    uint16_t width,
    uint16_t height,
    uint16_t window,
    ONE_CB one_cb
)
{
    ImGui::SetNextWindowPos(ImVec2(left, top));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("##zero", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    one_cb(left, top, width, height, r_utils::r_string_utils::format("%d_onebyone_%d", window, 0));
    ImGui::End();
}

// Callbacks we need:
//     - get_data
//         - when called: on entry, when range start time or range end time changes
//         - what it does: returns the video segments for the range, returns the motion data for the range
//     - control_bar
//         - when called: on entry, when the playhead is position, on range change
//     - Live click
//         - 
//
// Notes:
//   The control_bar_state protects the range start time going ahead of the range end time (and vice verse), but
//   it should probably proctect the range start time from going head of the current positionted time and the range
//   end time similarly.

template<typename CONTROL_BAR_SLIDER_CB, typename CONTROL_BAR_BUTTON_CB, typename UPDATE_DATA_CB, typename EXPORT_CB>
void control_bar(
    uint16_t left,
    uint16_t top,
    uint16_t width,
    uint16_t height,
    uint32_t duration_seconds,
    uint16_t window,
    bool show_motion_events,
    control_bar_state& cbs,
    CONTROL_BAR_SLIDER_CB control_bar_slider_cb,
    CONTROL_BAR_BUTTON_CB control_bar_button_cb,
    UPDATE_DATA_CB update_data_cb,
    EXPORT_CB export_cb
)
{
    if(!cbs.entered)
    {
        cbs.playhead_pos = 1000;
        cbs.live();
        auto name = r_utils::r_string_utils::format("%d_onebyone_%d", window, 0);
        control_bar_slider_cb(name, cbs.tr.time_in_range(0, 1000, cbs.playhead_pos));
        control_bar_button_cb(name, CONTROL_BAR_BUTTON_LIVE);

        cbs.entered = true;
    }

    if(cbs.need_update_data)
    {
        update_data_cb(cbs);
        cbs.need_update_data = false;
    }

    ImGui::SetNextWindowPos(ImVec2(left, top));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("##control_bar", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 p = ImVec2(left, top);

    ImGui::PushItemWidth(width);

    // Overall layout
    auto left_box_left = left;
    auto left_box_top = top;
    auto left_box_width = width * 0.16665f;
    auto left_box_height = height;
    auto center_box_left = left + left_box_width;
    auto center_box_top = top;
    //auto center_box_width = width * 0.66665f;
    auto center_box_width = width * 0.78f;
    auto center_box_height = height;
    auto right_box_left = left + left_box_width + center_box_width;
    auto right_box_top = top;
    auto right_box_width = width * 0.06f;
    auto right_box_height = height;

    auto text_line_height = ImGui::GetTextLineHeight();

    // top line
    auto top_line_top = center_box_top + 4;
    auto top_line_button_dim = text_line_height * 0.75f;
    ImGui::PushFont(r_ui_utils::fonts["16.00"].roboto_regular);
    ImGui::SetCursorScreenPos(ImVec2(center_box_left, top_line_top));
    if(ImGui::Button("-", ImVec2(top_line_button_dim, top_line_button_dim)))
        cbs.set_timerange_minutes(cbs.get_timerange_minutes() - 10);
    ImGui::SameLine();
    if(ImGui::Button("+", ImVec2(top_line_button_dim, top_line_button_dim)))
        cbs.set_timerange_minutes(cbs.get_timerange_minutes() + 10);
    ImGui::SameLine();
    auto timerange_s = r_utils::r_string_utils::format("%u minute time range", cbs.get_timerange_minutes());
    ImGui::Text("%s", timerange_s.c_str());
    ImGui::PopFont();

    // draw live button
    ImGui::PushFont(r_ui_utils::fonts["16.00"].roboto_regular);
    auto live_button_x = (center_box_left + center_box_width) - 60;
    ImGui::SetCursorScreenPos(ImVec2((float)live_button_x, (float)top_line_top));
    if(ImGui::Button("Go Live"))
    {
        cbs.playhead_pos = 1000;
        cbs.live();
        auto name = r_utils::r_string_utils::format("%d_onebyone_%d", window, 0);
        control_bar_slider_cb(name, cbs.tr.time_in_range(0, 1000, cbs.playhead_pos));
        control_bar_button_cb(name, CONTROL_BAR_BUTTON_LIVE);
        update_data_cb(cbs);
    }
    ImGui::PopFont();

    // draw export button
    ImGui::PushFont(r_ui_utils::fonts["16.00"].roboto_regular);
    auto export_button_x = (center_box_left + center_box_width) - 260;
    ImGui::SetCursorScreenPos(ImVec2((float)export_button_x, (float)top_line_top));
    bool export_clicked = false;
    bool finish_export_clicked = false;
    if(cbs.exp_state == EXPORT_STATE_CONFIGURING)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.3671f, 0.7695f, 0.5078f));
        finish_export_clicked = ImGui::Button("Finish Export");
        ImGui::PopStyleColor();
    }
    else export_clicked = ImGui::Button("Export ");

    if(export_clicked)
    {
        cbs.exp_state = EXPORT_STATE_CONFIGURING;
        cbs.export_start_time = cbs.tr.time_in_range(0, 1000, cbs.playhead_pos);
        cbs.playhead_pos += 50;
        if(cbs.playhead_pos > 1000)
            cbs.playhead_pos = 1000;
    }
    if(finish_export_clicked)
    {
        cbs.exp_state = EXPORT_STATE_STARTED;
        export_cb(cbs.export_start_time, cbs.tr.time_in_range(0, 1000, cbs.playhead_pos), cbs);
    }
    ImGui::PopFont();

    if(cbs.exp_state == EXPORT_STATE_CONFIGURING)
    {
        ImGui::PushFont(r_ui_utils::fonts["16.00"].roboto_regular);
        auto export_cancel_button_x = (center_box_left + center_box_width) - 160;
        ImGui::SetCursorScreenPos(ImVec2((float)export_cancel_button_x, (float)top_line_top));
        ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
        if(ImGui::Button("Cancel"))
        {
            cbs.exp_state = EXPORT_STATE_NONE;
        }
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    // Playhead
    cbs.playhead_pos = (std::max)((std::min)(cbs.playhead_pos, 1000), 0);

    auto slider_height = 2 * text_line_height;
    auto slider_top = center_box_top + text_line_height + 5;

    draw_list->AddRectFilled(
        ImVec2(center_box_left, slider_top),
        ImVec2(center_box_left + center_box_width, slider_top + slider_height),
        ImColor(ImVec4(0.2f, 0.2f, 0.2f, 1.0f))
    );

    auto bar_duration_millis = cbs.tr.duration_milliseconds();

    // Draw contents... 
    auto contents_top = slider_top + 5;
    auto contents_bottom = (slider_top + slider_height) - 5;   
    for(auto s : cbs.segments)
    {
        if(s.end > cbs.tr.get_end())
            s.end = cbs.tr.get_end();
        int64_t l_millis = std::chrono::duration_cast<std::chrono::milliseconds>(s.start - cbs.tr.get_start()).count();
        int64_t r_millis = std::chrono::duration_cast<std::chrono::milliseconds>(s.end - cbs.tr.get_start()).count();

        auto l = center_box_left + fabs((((double)l_millis / (double)bar_duration_millis) * (double)center_box_width));
        auto r = center_box_left + fabs((((double)r_millis / (double)bar_duration_millis) * (double)center_box_width));

        draw_list->AddRectFilled(
            ImVec2((l < r)?l:r, contents_top),
            ImVec2((r > l)?r:l, contents_bottom),
            ImColor(ImVec4(0.286f, 0.415f, 0.505f, 1.0f)) // 0x496a81 == 73 106 129
        );
    }

    if(cbs.exp_state == EXPORT_STATE_CONFIGURING)
    {
        if(cbs.tr.time_is_in_range(cbs.export_start_time))
        {
            auto export_start = cbs.tr.time_to_range(cbs.export_start_time, 0, 1000);

            auto tick_x = ((float)export_start / 1000.0f) * center_box_width;

            draw_list->AddRectFilled(
                ImVec2((center_box_left + tick_x)-2, contents_top),
                ImVec2((center_box_left + tick_x)+2, contents_bottom),
                ImColor(ImVec4(0.0f, 1.0f, 0.0f, 1.0f))
            );
        }
    }

    // Draw events...
    auto event_top = slider_top + (0.33 * slider_height);
    auto event_bottom = slider_top + (0.66 * slider_height);
    if(show_motion_events)
    {
        for(auto me : cbs.motion_events)
        {
            
            double l_millis = std::chrono::duration_cast<std::chrono::milliseconds>(me.start - cbs.tr.get_start()).count();
            double r_millis = std::chrono::duration_cast<std::chrono::milliseconds>(me.end - cbs.tr.get_start()).count();

            auto l = center_box_left + fabs(((l_millis / bar_duration_millis) * center_box_width));
            auto r = center_box_left + fabs(((r_millis / bar_duration_millis) * center_box_width));

            draw_list->AddRectFilled(
                ImVec2(l, event_top),
                ImVec2(r, event_bottom),
                ImColor(ImVec4(0.400f, 0.600f, 0.607f, 1.0f)) // 0x66999b == 102 153 155 == 0.4f 0.6f 0.607f
            );
        }
    }

    // draw playhead...
    auto playhead_x = (center_box_left + (((double)cbs.playhead_pos / 1000.0) * (double)center_box_width)) - (cbs.playhead_width / 2);
    auto playhead_top = slider_top;
    draw_list->AddRectFilled(
        ImVec2(playhead_x, playhead_top),
        ImVec2((playhead_x + cbs.playhead_width), (playhead_top + slider_height)),
        ImColor(ImVec4(0.0f, 1.0f, 0.0f, 1.0f))
    );

    // draw playhead text
    ImGui::PushFont(r_ui_utils::fonts["16.00"].roboto_regular);
    auto playhead_pos_s = cbs.playhead_pos_s();
    auto text_size = ImGui::CalcTextSize(playhead_pos_s.c_str());
    auto playhead_text_x = playhead_x - (text_size.x / 2);
    if(playhead_text_x < center_box_left)
        playhead_text_x = center_box_left;
    auto center_box_right = center_box_left + center_box_width;
    if(playhead_x > (center_box_right - text_size.x))
        playhead_text_x = (center_box_right - text_size.x);
    auto playhead_text_y = (playhead_top + (text_line_height * 2));
    ImGui::SetCursorScreenPos(ImVec2(playhead_text_x, playhead_text_y));
    ImGui::Text("%s",cbs.playhead_pos_s().c_str());
    ImGui::PopFont();

    // playhead dragging...
    if(ImGui::IsMouseHoveringRect(ImVec2(center_box_left, slider_top), ImVec2(center_box_left + center_box_width, slider_top + slider_height)) && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        cbs.dragging = true;

    if(cbs.dragging)
    {
        uint16_t phx = (std::min)((std::max)(ImGui::GetMousePos().x, center_box_left), (center_box_left + center_box_width)) - (cbs.playhead_width/2);

        cbs.playhead_pos = (std::max)((std::min)((int)(((double)(phx - center_box_left) / (double)(center_box_width)) * 1000.0), 1000), 0);

        control_bar_slider_cb(r_utils::r_string_utils::format("%d_onebyone_%d", window, 0), cbs.tr.time_in_range(0, 1000, cbs.playhead_pos));

        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            cbs.dragging = false;
    }

    auto rs_backward_forward_button_dim = text_line_height * 1.25;
    auto center_line_y = event_top + ((event_bottom - event_top) / 2);

    // draw range start backward button
    ImGui::PushID("backward");
    ImGui::PushFont(r_ui_utils::fonts["14.00"].roboto_regular);
    auto backward_button_x = ((left_box_left + left_box_width) - rs_backward_forward_button_dim) - (text_line_height / 2);
    auto backward_button_y = center_line_y - (rs_backward_forward_button_dim / 2);
    ImGui::SetCursorScreenPos(ImVec2((float)backward_button_x, (float)backward_button_y));
    if(ImGui::Button("<", ImVec2(rs_backward_forward_button_dim, rs_backward_forward_button_dim)))
    {
        cbs.backward(std::chrono::minutes(10));
        update_data_cb(cbs);
    }
    ImGui::PopFont();
    ImGui::PopID();

    // draw range end forward button
    ImGui::PushID("forward");
    ImGui::PushFont(r_ui_utils::fonts["14.00"].roboto_regular);
    auto forward_button_x = right_box_left + (text_line_height / 2);
    auto forward_button_y = center_line_y - (rs_backward_forward_button_dim / 2);
    ImGui::SetCursorScreenPos(ImVec2((float)forward_button_x, (float)forward_button_y));
    if(ImGui::Button(">", ImVec2(rs_backward_forward_button_dim, rs_backward_forward_button_dim)))
    {
        cbs.forward(std::chrono::minutes(10));
        update_data_cb(cbs);
    }
    ImGui::PopFont();
    ImGui::PopID();

    ImGui::End();
}

template<typename PANE_CB>
void two_by_two(
    uint16_t left,
    uint16_t top,
    uint16_t width,
    uint16_t height,
    uint16_t window,
    PANE_CB pane_cb
)
{
    const std::vector<std::string> labels = {
        "##zero",
        "##one",
        "##two",
        "##three"
    };

    uint16_t sub_window_width = width / 2;
    uint16_t sub_window_height = height / 2;

    uint16_t index = 0;
    uint16_t pos_x = left, pos_y = top;
    for(uint16_t i = 0; i < 2; ++i)
    {
        pos_y = top + (sub_window_height * i);

        for(uint16_t ii = 0; ii < 2; ++ii)
        {
            pos_x = left + (sub_window_width * ii);

            ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y));
            ImGui::SetNextWindowSize(ImVec2(sub_window_width, sub_window_height));
            ImGui::Begin(labels[index].c_str(), NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
            pane_cb(pos_x, pos_y, sub_window_width, sub_window_height, r_utils::r_string_utils::format("%d_twobytwo_%d", window, index));
            ImGui::End();

            ++index;
        }
    }
}

template<typename PANE_CB>
void four_by_four(
    uint16_t left,
    uint16_t top,
    uint16_t width,
    uint16_t height,
    uint16_t window,
    PANE_CB pane_cb
)
{
    const std::vector<std::string> labels = {
        "##zero",
        "##one",
        "##two",
        "##three",
        "##four",
        "##five",
        "##six",
        "##seven",
        "##eight",
        "##nine",
        "##ten",
        "##eleven",
        "##twelve",
        "##thirteen",
        "##fourteen",
        "##fifteen"
    };

    uint16_t sub_window_width = width / 4;
    uint16_t sub_window_height = height / 4;

    uint16_t index = 0;
    uint16_t pos_x = left, pos_y = top;
    for(uint16_t i = 0; i < 4; ++i)
    {
        pos_y = top + (sub_window_height * i);

        for(uint16_t ii = 0; ii < 4; ++ii)
        {
            pos_x = left + (sub_window_width * ii);

            ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y));
            ImGui::SetNextWindowSize(ImVec2(sub_window_width, sub_window_height));
            ImGui::Begin(labels[index].c_str(), NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
            pane_cb(pos_x, pos_y, sub_window_width, sub_window_height, r_utils::r_string_utils::format("%d_fourbyfour_%d", window, index));
            ImGui::End();

            ++index;
        }
    }
}

template<typename PANE_CB, typename CONTROL_BAR_SLIDER_CB, typename CONTROL_BAR_BUTTON_CB, typename UPDATE_DATA_CB, typename EXPORT_CB>
void main_client(
    layout l,
    uint16_t x,
    uint16_t y,
    uint16_t w,
    uint16_t h,
    uint16_t window,
    main_client_state& mcs,
    configure_state& cs,
    PANE_CB pane_cb,
    CONTROL_BAR_SLIDER_CB control_bar_slider_cb,
    CONTROL_BAR_BUTTON_CB control_bar_button_cb,
    UPDATE_DATA_CB update_data_cb,
    EXPORT_CB export_cb
)
{
    if(l == LAYOUT_ONE_BY_ONE)
    {
        uint16_t control_bar_height = (uint16_t)(4 * ImGui::GetTextLineHeightWithSpacing());
        uint16_t video_pane_height = h - control_bar_height;
        vision::one_by_one(
            x, y, w, video_pane_height,
            window,
            pane_cb
        );
        auto si = cs.get_stream_info("0_onebyone_0");
        vision::control_bar(
            x, y + video_pane_height, w, control_bar_height,
            600,
            window,
            (!si.is_null())?si.value().do_motion_detection:false,
            mcs.obos.cbs,
            control_bar_slider_cb,
            control_bar_button_cb,
            update_data_cb,
            export_cb
        );
    }
    else if(l == LAYOUT_TWO_BY_TWO)
    {
        vision::two_by_two(
            x, y, w, h,
            window,
            pane_cb
        );
    }
    else if(l == LAYOUT_FOUR_BY_FOUR)
    {
        vision::four_by_four(
            x, y, w, h,
            window,
            pane_cb
        );
    }
}

struct sidebar_list_ui_item
{
    std::string label;
    std::string sub_label;
    std::string camera_id;
    bool do_motion_detection;
};

template<typename ITEM_CLICK_CB>
void sidebar_list(
    ImGuiContext* GImGui,
    vision::configure_state& cs,
    int selected,
    uint16_t width,
    std::vector<sidebar_list_ui_item>& labels,
    ITEM_CLICK_CB item_click_cb
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

        ImGui::SetCursorPos(ImVec2(pos.x, pos.y));

        auto name = r_utils::r_string_utils::format("##Object %d", i);

        ImGui::SetCursorPos(ImVec2(pos.x, pos.y));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(20, 52, 77, 200)); // modify a style color. always use this if you modify the style after NewFrame().
        ImGui::PushStyleColor(ImGuiCol_NavHighlight, IM_COL32(255, 0, 0, 200));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(25, 47, 63, 200));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(49, 55, 62, 200));
        if(ImGui::Selectable(name.c_str(), i == selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2((float)width, 50)))
            item_click_cb(i, ImGui::IsMouseDoubleClicked(0));
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();

        if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            ImGui::SetDragDropPayload("camera_id", labels[i].camera_id.c_str(), labels[i].camera_id.size());
            ImGui::Text("%s", labels[i].label.c_str());
            ImGui::EndDragDropSource();
        }

        // Center button in region to right of text
        std::string label = labels[i].label;
        std::string sub_label = labels[i].sub_label;

        ImGui::PushFont(r_ui_utils::fonts["24.00"].roboto_bold);
        ImGui::SetCursorPos(ImVec2(pos.x+15, pos.y+10));
        ImGui::Text("%s", label.c_str());
        ImGui::PopFont();

        auto cursor_pos = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddLine(ImVec2(cursor_pos.x, cursor_pos.y+15), ImVec2(cursor_pos.x + width, cursor_pos.y+15), IM_COL32(40, 40, 40, 200));

        pos.y += 55;

        ImGui::PopID();
    }
}

}

#endif
