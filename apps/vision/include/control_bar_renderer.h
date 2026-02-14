#ifndef __vision_control_bar_renderer_h
#define __vision_control_bar_renderer_h

#include "imgui/imgui.h"
#include "control_bar.h"
#include "error_handling.h"
#include "timeline_constants.h"
#include "r_utils/r_string_utils.h"
#include "r_ui_utils/font_catalog.h"
#include "font_keys.h"
#include <cstdint>
#include <functional>
#include <chrono>

namespace vision
{

// Forward declarations
struct segment;
struct motion_event;

// Layout information for the control bar
struct control_bar_layout
{
    uint16_t left;
    uint16_t top;
    uint16_t width;
    uint16_t height;
    uint16_t window;
    uint32_t duration_seconds;
};

// Layout calculations result structure
struct control_bar_calculated_layout
{
    float center_box_left;
    float center_box_width;
    float center_box_top;
    float slider_top;
    float slider_height;
    float contents_top;
    float contents_bottom;
    float event_top;
    float event_bottom;
    float text_line_height;
};

class control_bar_renderer
{
public:
    control_bar_renderer();
    ~control_bar_renderer();

    // Main rendering function
    template<typename CONTROL_BAR_SLIDER_CB, typename CONTROL_BAR_BUTTON_CB, typename UPDATE_DATA_CB, typename EXPORT_CB>
    void render(
        const control_bar_layout& layout,
        bool show_motion_events,
        control_bar_state& cbs,
        CONTROL_BAR_SLIDER_CB control_bar_slider_cb,
        CONTROL_BAR_BUTTON_CB control_bar_button_cb,
        UPDATE_DATA_CB update_data_cb,
        EXPORT_CB export_cb,
        bool playing
    );
    
    // Public rendering methods for gradual refactoring (with error handling)
    bool render_background(ImDrawList* draw_list, const control_bar_layout& layout);
    bool render_segments(ImDrawList* draw_list, const control_bar_state& cbs, const control_bar_layout& layout);
    bool render_motion_events(ImDrawList* draw_list, const control_bar_state& cbs, const control_bar_layout& layout);
    bool render_analytics_events(ImDrawList* draw_list, const control_bar_state& cbs, const control_bar_layout& layout);
    bool render_playhead(ImDrawList* draw_list, const control_bar_state& cbs, const control_bar_layout& layout, bool playing);
    bool render_tick_marks(ImDrawList* draw_list, const control_bar_state& cbs, const control_bar_layout& layout);
    
    // UI control rendering methods
    void render_timerange_text(const control_bar_layout& layout, const control_bar_state& cbs, const control_bar_calculated_layout& calc);
    
    template<typename EXPORT_CB>
    void render_export_controls(const control_bar_layout& layout, control_bar_state& cbs, const control_bar_calculated_layout& calc, const std::string& stream_name, EXPORT_CB export_cb);
    
    template<typename CONTROL_BAR_BUTTON_CB, typename UPDATE_DATA_CB>
    void render_play_live_button(const control_bar_layout& layout, control_bar_state& cbs, const control_bar_calculated_layout& calc,
                                const std::string& stream_name, CONTROL_BAR_BUTTON_CB control_bar_button_cb, UPDATE_DATA_CB update_data_cb, bool playing);

    template<typename CONTROL_BAR_SLIDER_CB>
    void handle_playhead_interaction(const control_bar_layout& layout, control_bar_state& cbs, const control_bar_calculated_layout& calc,
                                   const std::string& stream_name, CONTROL_BAR_SLIDER_CB control_bar_slider_cb);

    template<typename UPDATE_DATA_CB>
    void render_navigation_buttons(const control_bar_layout& layout, control_bar_state& cbs, const control_bar_calculated_layout& calc,
                                 const std::string& stream_name, UPDATE_DATA_CB update_data_cb);

    // Layout calculations
    control_bar_calculated_layout calculate_layout(const control_bar_layout& layout) const;

private:
    // Helper methods
    float calculate_position_ratio(int64_t time_millis, int64_t duration_millis) const;
};

// Template method implementations (must be in header)

template<typename EXPORT_CB>
void control_bar_renderer::render_export_controls(const control_bar_layout& layout, control_bar_state& cbs, const control_bar_calculated_layout& calc, const std::string& stream_name, EXPORT_CB export_cb)
{
    auto top_line_top = calc.center_box_top + timeline_constants::TOP_BUTTON_OFFSET;
    auto right_edge = calc.center_box_left + calc.center_box_width;

    bool export_clicked = false;
    bool finish_export_clicked = false;

    if(cbs.exp_state == EXPORT_STATE_CONFIGURING)
    {
        // When configuring export: Finish Export | Cancel | Play | Go Live
        // From right with 10px spacing: Go Live(80) @ -90, Play(80) @ -180, Cancel(90) @ -280, Finish Export(120) @ -410
        auto finish_export_x = right_edge - 410;
        ImGui::SetCursorScreenPos(ImVec2((float)finish_export_x, (float)top_line_top));
        ImGui::PushStyleColor(ImGuiCol_Button, timeline_constants::colors::EXPORT_BUTTON_ACTIVE);
        finish_export_clicked = ImGui::Button("Finish Export", ImVec2(120, 0));
        ImGui::PopStyleColor();
    }
    else
    {
        // Normal export button: Export | Play | Go Live
        // From right with 10px spacing: Go Live(80) @ -90, Play(80) @ -180, Export(80) @ -270
        auto export_button_x = right_edge - 270;
        ImGui::SetCursorScreenPos(ImVec2((float)export_button_x, (float)top_line_top));
        export_clicked = ImGui::Button("Export", ImVec2(80, 0));
    }

    if(export_clicked)
    {
        cbs.exp_state = EXPORT_STATE_CONFIGURING;
        cbs.export_start_time = cbs.tr.time_in_range(0, timeline_constants::PLAYHEAD_MAX_POSITION, cbs.playhead_pos);
        cbs.playhead_pos += timeline_constants::EXPORT_PLAYHEAD_ADVANCE;
        if(cbs.playhead_pos > timeline_constants::PLAYHEAD_MAX_POSITION)
            cbs.playhead_pos = timeline_constants::PLAYHEAD_MAX_POSITION;
    }

    if(finish_export_clicked)
    {
        cbs.exp_state = EXPORT_STATE_STARTED;
        export_cb(stream_name, cbs.export_start_time, cbs.tr.time_in_range(0, timeline_constants::PLAYHEAD_MAX_POSITION, cbs.playhead_pos), cbs);
    }

    // Cancel button (shown during export configuration)
    if(cbs.exp_state == EXPORT_STATE_CONFIGURING)
    {
        auto export_cancel_button_x = right_edge - 280;
        ImGui::SetCursorScreenPos(ImVec2((float)export_cancel_button_x, (float)top_line_top));
        ImGui::PushStyleColor(ImGuiCol_Button, timeline_constants::colors::CANCEL_BUTTON);
        if(ImGui::Button("Cancel", ImVec2(90, 0)))
        {
            cbs.exp_state = EXPORT_STATE_NONE;
        }
        ImGui::PopStyleColor();
    }
}

template<typename CONTROL_BAR_BUTTON_CB, typename UPDATE_DATA_CB>
void control_bar_renderer::render_play_live_button(const control_bar_layout& layout, control_bar_state& cbs, const control_bar_calculated_layout& calc,
                                                   const std::string& stream_name, CONTROL_BAR_BUTTON_CB control_bar_button_cb, UPDATE_DATA_CB update_data_cb, bool playing)
{
    auto top_line_top = calc.center_box_top + timeline_constants::TOP_BUTTON_OFFSET;
    bool is_at_live = (cbs.playhead_pos >= timeline_constants::PLAYHEAD_MAX_POSITION);

    // Calculate button positions based on how many buttons we need
    auto right_edge = calc.center_box_left + calc.center_box_width;
    float play_button_x, live_button_x;

    // Adjust positions if export is configuring (need room for Finish Export and Cancel buttons)
    bool export_active = (cbs.exp_state == EXPORT_STATE_CONFIGURING);

    if (!is_at_live && !playing)
    {
        // Show both buttons: Play and Go Live
        if (export_active)
        {
            // Export configuring mode: positions match export button layout
            play_button_x = right_edge - 180;
            live_button_x = right_edge - 90;
        }
        else
        {
            // Normal mode: positions match export button layout
            play_button_x = right_edge - 180;
            live_button_x = right_edge - 90;
        }

        ImGui::SetCursorScreenPos(ImVec2(play_button_x, (float)top_line_top));
        if(ImGui::Button("Play", ImVec2(80, 0)))
        {
            control_bar_button_cb(stream_name, CONTROL_BAR_BUTTON_PLAY);
        }

        ImGui::SetCursorScreenPos(ImVec2(live_button_x, (float)top_line_top));
        if(ImGui::Button("Go Live", ImVec2(80, 0)))
        {
            cbs.live();
            control_bar_button_cb(stream_name, CONTROL_BAR_BUTTON_LIVE);
            update_data_cb(stream_name, cbs);
        }
    }
    else if (!is_at_live && playing)
    {
        // Show only Go Live button when playing in the past (same position in both modes)
        live_button_x = right_edge - 90;

        ImGui::SetCursorScreenPos(ImVec2(live_button_x, (float)top_line_top));
        if(ImGui::Button("Go Live", ImVec2(80, 0)))
        {
            cbs.live();
            control_bar_button_cb(stream_name, CONTROL_BAR_BUTTON_LIVE);
            update_data_cb(stream_name, cbs);
        }
    }
    // When at live position (is_at_live == true), show no buttons
}

template<typename CONTROL_BAR_SLIDER_CB>
void control_bar_renderer::handle_playhead_interaction(const control_bar_layout& layout, control_bar_state& cbs, const control_bar_calculated_layout& calc,
                                                      const std::string& stream_name, CONTROL_BAR_SLIDER_CB control_bar_slider_cb)
{
    // Handle playhead interaction
    if(ImGui::IsMouseHoveringRect(ImVec2(calc.center_box_left, calc.slider_top),
                                  ImVec2(calc.center_box_left + calc.center_box_width, calc.slider_top + calc.slider_height)) &&
       ImGui::IsMouseDown(ImGuiMouseButton_Left))
        cbs.dragging = true;

    if(cbs.dragging)
    {
        uint16_t phx = (uint16_t)(std::min)((std::max)(ImGui::GetMousePos().x, calc.center_box_left),
                                           (calc.center_box_left + calc.center_box_width)) - (cbs.playhead_width/2);
        cbs.playhead_pos = (uint16_t)(std::max)((std::min)((int)(((double)(phx - calc.center_box_left) / (double)(calc.center_box_width)) * 1000.0), 1000), 0);
        control_bar_slider_cb(stream_name, cbs.tr.time_in_range(0, 1000, cbs.playhead_pos));
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            cbs.dragging = false;
    }
}

template<typename UPDATE_DATA_CB>
void control_bar_renderer::render_navigation_buttons(const control_bar_layout& layout, control_bar_state& cbs, const control_bar_calculated_layout& calc,
                                                    const std::string& stream_name, UPDATE_DATA_CB update_data_cb)
{
    auto rs_backward_forward_button_dim = calc.text_line_height * 1.25;
    auto center_line_y = calc.event_top + ((calc.event_bottom - calc.event_top) / 2);
    auto left_box_width = layout.width * timeline_constants::LEFT_PANEL_WIDTH_RATIO;
    auto right_box_left = layout.left + left_box_width + calc.center_box_width;

    ImGui::PushFont(r_ui_utils::fonts[vision::get_font_key_14()].roboto_regular);

    // Backward button
    ImGui::PushID("backward");
    auto backward_button_x = ((layout.left + left_box_width) - rs_backward_forward_button_dim) - (calc.text_line_height / 2);
    auto backward_button_y = center_line_y - (rs_backward_forward_button_dim / 2);
    ImGui::SetCursorScreenPos(ImVec2((float)backward_button_x, (float)backward_button_y));
    if(ImGui::Button("<", ImVec2((float)rs_backward_forward_button_dim, (float)rs_backward_forward_button_dim)))
    {
        cbs.backward(std::chrono::minutes(10));
        update_data_cb(stream_name, cbs);
    }
    ImGui::PopID();

    // Forward button
    ImGui::PushID("forward");
    auto forward_button_x = right_box_left + (calc.text_line_height / 2);
    auto forward_button_y = center_line_y - (rs_backward_forward_button_dim / 2);
    ImGui::SetCursorScreenPos(ImVec2((float)forward_button_x, (float)forward_button_y));
    if(ImGui::Button(">", ImVec2((float)rs_backward_forward_button_dim, (float)rs_backward_forward_button_dim)))
    {
        cbs.forward(std::chrono::minutes(10));
        update_data_cb(stream_name, cbs);
    }
    ImGui::PopID();

    ImGui::PopFont();
}

} // namespace vision

#endif