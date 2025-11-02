
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
#include "control_bar_renderer.h"
#include "timeline_constants.h"
#include "error_handling.h"

namespace vision
{

struct one_by_one_state
{
    control_bar_state cbs;
};

struct main_client_state
{
    one_by_one_state obos;
    std::string selected_stream_name {"0_onebyone_0"};  // Track selected stream for multi-view timeline
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
        //ImGuiContext& g = *GImGui;
        //ImGuiWindow* window = g.CurrentWindow;
        //ImVec2 pos_before = window->DC.CursorPos;

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
        //ImGuiContext& g = *GImGui;
        //ImGuiWindow* window = g.CurrentWindow;
        //ImVec2 pos_before = window->DC.CursorPos;

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
    std::string window_name = r_utils::r_string_utils::format("%d_onebyone_%d", window, 0);
    one_cb(left, top, width, height, window_name);
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
    const std::string& stream_name,
    CONTROL_BAR_SLIDER_CB control_bar_slider_cb,
    CONTROL_BAR_BUTTON_CB control_bar_button_cb,
    UPDATE_DATA_CB update_data_cb,
    EXPORT_CB export_cb,
    bool playing
)
{
    if(!cbs.entered)
    {
        cbs.playhead_pos = timeline_constants::PLAYHEAD_MAX_POSITION;
        cbs.live();
        control_bar_slider_cb(stream_name, cbs.tr.time_in_range(0, 1000, cbs.playhead_pos));
        control_bar_button_cb(stream_name, CONTROL_BAR_BUTTON_LIVE);

        cbs.entered = true;
    }
    
    // Update segment data periodically
    static auto last_timeline_update = std::chrono::steady_clock::now();
    static auto last_live_scroll_update = std::chrono::steady_clock::now();
    auto current_steady_time = std::chrono::steady_clock::now();
    
    // More frequent updates when in live mode for smooth scrolling
    bool is_at_live = (cbs.playhead_pos >= timeline_constants::PLAYHEAD_MAX_POSITION);
    if (is_at_live && playing)
    {
        // Update every 1 second for live mode scrolling
        if(std::chrono::duration_cast<std::chrono::milliseconds>(current_steady_time - last_live_scroll_update).count() >= 1000)
        {
            cbs.live();  // This updates the range and sets need_update_data
            last_live_scroll_update = current_steady_time;
        }
    }
    
    // Regular segment data updates
    if(std::chrono::duration_cast<std::chrono::milliseconds>(current_steady_time - last_timeline_update).count() >= timeline_constants::SEGMENT_UPDATE_INTERVAL.count())
    {
        if (!is_at_live || !playing)
        {
            // Only refresh segment data if not in live mode
            cbs.need_update_data = true;
        }
        last_timeline_update = current_steady_time;
    }

    if(cbs.need_update_data)
    {
        update_data_cb(stream_name, cbs);
        cbs.need_update_data = false;
    }

    ImGui::SetNextWindowPos(ImVec2(left, top));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("##control_bar", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::PushItemWidth(width);

    // Create a renderer instance and layout info for gradual refactoring
    static control_bar_renderer renderer;
    control_bar_layout render_layout;
    render_layout.left = left;
    render_layout.top = top;
    render_layout.width = width;
    render_layout.height = height;
    render_layout.window = window;
    render_layout.duration_seconds = duration_seconds;
    
    // Use renderer for all UI components
    auto calc = renderer.calculate_layout(render_layout);
    
    // Render timerange text
    renderer.render_timerange_text(render_layout, cbs, calc);
    
    // Render export controls
    renderer.render_export_controls(render_layout, cbs, calc, stream_name, export_cb);
    
    // Render play/live button
    renderer.render_play_live_button(render_layout, cbs, calc, stream_name, control_bar_button_cb, update_data_cb, playing);


    // Playhead bounds check
    cbs.playhead_pos = (std::max)((std::min)(cbs.playhead_pos, 1000), 0);

    // Render background and segments with error handling
    if (!renderer.render_background(draw_list, render_layout))
    {
        R_LOG_ERROR("Failed to render timeline background");
    }
    
    if (!renderer.render_segments(draw_list, cbs, render_layout))
    {
        R_LOG_ERROR("Failed to render timeline segments");
    }
    
    // Render tick marks
    if (!renderer.render_tick_marks(draw_list, cbs, render_layout))
    {
        R_LOG_ERROR("Failed to render timeline tick marks");
    }

    // Render export overlay if needed
    if(cbs.exp_state == EXPORT_STATE_CONFIGURING)
    {
        if(cbs.tr.time_is_in_range(cbs.export_start_time))
        {
            auto inner_calc = renderer.calculate_layout(render_layout);
            auto export_start = cbs.tr.time_to_range(cbs.export_start_time, 0, 1000);
            auto tick_x = ((float)export_start / 1000.0f) * inner_calc.center_box_width;

            draw_list->AddRectFilled(
                ImVec2((inner_calc.center_box_left + tick_x)-2, inner_calc.contents_top),
                ImVec2((inner_calc.center_box_left + tick_x)+2, inner_calc.contents_bottom),
                ImColor(timeline_constants::colors::EXPORT_MARKER)
            );
        }
    }

    // Render motion events and playhead with error handling
    if(show_motion_events)
    {
        if (!renderer.render_motion_events(draw_list, cbs, render_layout))
        {
            R_LOG_ERROR("Failed to render motion events");
        }
    }
    
    // Render analytics events (people and cars)
    if (!renderer.render_analytics_events(draw_list, cbs, render_layout))
    {
        R_LOG_ERROR("Failed to render analytics events");
    }
        
    if (!renderer.render_playhead(draw_list, cbs, render_layout, playing))
    {
        R_LOG_ERROR("Failed to render playhead");
    }

    // Handle playhead interaction using renderer
    renderer.handle_playhead_interaction(render_layout, cbs, calc, stream_name, control_bar_slider_cb);

    // Render navigation controls
    renderer.render_navigation_buttons(render_layout, cbs, calc, stream_name, update_data_cb);

    ImGui::End();
}

template<typename PANE_CB>
void two_by_two(
    uint16_t left,
    uint16_t top,
    uint16_t width,
    uint16_t height,
    uint16_t window,
    PANE_CB pane_cb,
    std::string& selected_stream_name
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
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin(labels[index].c_str(), NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
            std::string window_name = r_utils::r_string_utils::format("%d_twobytwo_%d", window, index);

            // Draw selection border if this is the selected stream
            if(!selected_stream_name.empty() && window_name == selected_stream_name)
            {
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                ImVec2 window_pos = ImGui::GetWindowPos();
                ImVec2 window_size = ImGui::GetWindowSize();
                draw_list->AddRect(
                    window_pos,
                    ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y),
                    IM_COL32(0, 150, 255, 255),  // Blue selection border
                    0.0f,  // No rounding
                    0,     // All corners
                    4.0f   // 4px thickness
                );
            }

            // Check for click to select this stream
            if(ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0))
            {
                selected_stream_name = window_name;
            }

            pane_cb(pos_x, pos_y, sub_window_width, sub_window_height, window_name);
            ImGui::End();
            ImGui::PopStyleVar();

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
    PANE_CB pane_cb,
    std::string& selected_stream_name
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
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin(labels[index].c_str(), NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
            std::string window_name = r_utils::r_string_utils::format("%d_fourbyfour_%d", window, index);

            // Draw selection border if this is the selected stream
            if(!selected_stream_name.empty() && window_name == selected_stream_name)
            {
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                ImVec2 window_pos = ImGui::GetWindowPos();
                ImVec2 window_size = ImGui::GetWindowSize();
                draw_list->AddRect(
                    window_pos,
                    ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y),
                    IM_COL32(0, 150, 255, 255),  // Blue selection border
                    0.0f,  // No rounding
                    0,     // All corners
                    4.0f   // 4px thickness
                );
            }

            // Check for click to select this stream
            if(ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0))
            {
                selected_stream_name = window_name;
            }

            pane_cb(pos_x, pos_y, sub_window_width, sub_window_height, window_name);
            ImGui::End();
            ImGui::PopStyleVar();

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
    EXPORT_CB export_cb,
    bool playing
)
{
    uint16_t control_bar_height = (uint16_t)(6 * ImGui::GetTextLineHeightWithSpacing());
    uint16_t video_pane_height = h - control_bar_height;

    if(l == LAYOUT_ONE_BY_ONE)
    {
        vision::one_by_one(
            x, y, w, video_pane_height,
            window,
            pane_cb
        );

        mcs.selected_stream_name = "0_onebyone_0";
        auto si = cs.get_stream_info(mcs.selected_stream_name);
        bool show_motion_events = (!si.is_null()) ? si.value().do_motion_detection : false;

        vision::control_bar(
            x, y + video_pane_height, w, control_bar_height,
            600,
            window,
            show_motion_events,
            mcs.obos.cbs,
            mcs.selected_stream_name,
            control_bar_slider_cb,
            control_bar_button_cb,
            update_data_cb,
            export_cb,
            playing
        );
    }
    else if(l == LAYOUT_TWO_BY_TWO)
    {
        // Initialize selected stream if empty or invalid for this layout
        if(mcs.selected_stream_name.find("twobytwo") == std::string::npos)
            mcs.selected_stream_name = "0_twobytwo_0";

        vision::two_by_two(
            x, y, w, video_pane_height,
            window,
            pane_cb,
            mcs.selected_stream_name
        );

        auto si = cs.get_stream_info(mcs.selected_stream_name);
        bool show_motion_events = (!si.is_null()) ? si.value().do_motion_detection : false;

        vision::control_bar(
            x, y + video_pane_height, w, control_bar_height,
            600,
            window,
            show_motion_events,
            mcs.obos.cbs,
            mcs.selected_stream_name,
            control_bar_slider_cb,
            control_bar_button_cb,
            update_data_cb,
            export_cb,
            playing
        );
    }
    else if(l == LAYOUT_FOUR_BY_FOUR)
    {
        // Initialize selected stream if empty or invalid for this layout
        if(mcs.selected_stream_name.find("fourbyfour") == std::string::npos)
            mcs.selected_stream_name = "0_fourbyfour_0";

        vision::four_by_four(
            x, y, w, video_pane_height,
            window,
            pane_cb,
            mcs.selected_stream_name
        );

        auto si = cs.get_stream_info(mcs.selected_stream_name);
        bool show_motion_events = (!si.is_null()) ? si.value().do_motion_detection : false;

        vision::control_bar(
            x, y + video_pane_height, w, control_bar_height,
            600,
            window,
            show_motion_events,
            mcs.obos.cbs,
            mcs.selected_stream_name,
            control_bar_slider_cb,
            control_bar_button_cb,
            update_data_cb,
            export_cb,
            playing
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
    for(int i = 0; i < (int)labels.size(); ++i)
    {
        auto label_size = ImGui::CalcTextSize(labels[i].label.c_str());
        auto sub_label_size = ImGui::CalcTextSize(labels[i].sub_label.c_str());

        if(label_size.x > largest_label)
            largest_label = label_size.x;
        if(sub_label_size.x > largest_label)
            largest_label = sub_label_size.x;
    }

    // selectable list
    for(int i = 0; i < (int)labels.size(); ++i)
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
