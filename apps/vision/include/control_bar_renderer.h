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
    float icon_scale {1.0f};  // scales the analytics detection icons (overlay uses < 1)
};

// Layout calculations result structure
struct control_bar_calculated_layout
{
    float center_box_left;
    float center_box_width;
    float center_box_top;
    float row_top;      // the control row: range, audio, time, buttons
    float row_height;
    float slider_top;
    float slider_height;
    float contents_top;
    float contents_bottom;
    float event_top;
    float event_bottom;
    float text_line_height;
};

// The height the bar needs in the current font: one control row over the
// timeline strip. Callers size the bar with this so it can't drift from what
// calculate_layout() draws.
inline float control_bar_preferred_height()
{
    return timeline_constants::TOP_BUTTON_OFFSET + ImGui::GetFrameHeight() +
           timeline_constants::ROW_GAP +
           (timeline_constants::TIMELINE_HEIGHT_MULTIPLIER * ImGui::GetTextLineHeight()) +
           timeline_constants::BOTTOM_MARGIN;
}

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
    
    // UI control rendering methods. The control row's left side flows
    // rightward (range text, zoom buttons) and its right side flows leftward
    // (Go live, Play, Export, ...); each takes the running x and advances it.
    void render_timerange_text(const control_bar_layout& layout, const control_bar_state& cbs, const control_bar_calculated_layout& calc);
    float render_zoom_buttons(const control_bar_layout& layout, control_bar_state& cbs, const control_bar_calculated_layout& calc);

    // The playhead's time in the space between the row's left and right
    // groups: the longest form that fits, or nothing.
    void render_playhead_time(const control_bar_state& cbs, const control_bar_calculated_layout& calc, float left_x, float right_x, bool live);

    template<typename EXPORT_CB>
    void render_export_controls(const control_bar_layout& layout, control_bar_state& cbs, const control_bar_calculated_layout& calc, const std::string& stream_name, EXPORT_CB export_cb, float& right_x);

    template<typename CONTROL_BAR_BUTTON_CB, typename UPDATE_DATA_CB>
    void render_play_live_button(const control_bar_layout& layout, control_bar_state& cbs, const control_bar_calculated_layout& calc,
                                const std::string& stream_name, CONTROL_BAR_BUTTON_CB control_bar_button_cb, UPDATE_DATA_CB update_data_cb, bool playing, float& right_x);

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
void control_bar_renderer::render_export_controls(const control_bar_layout& layout, control_bar_state& cbs, const control_bar_calculated_layout& calc, const std::string& stream_name, EXPORT_CB export_cb, float& right_x)
{
    const float gap = timeline_constants::ROW_BUTTON_GAP;

    bool export_clicked = false;
    bool finish_export_clicked = false;

    if(cbs.exp_state == EXPORT_STATE_CONFIGURING)
    {
        // When configuring export: Finish export | Cancel | (Play | Go live)
        right_x -= 90.0f;
        ImGui::SetCursorScreenPos(ImVec2(right_x, calc.row_top));
        if(ImGui::Button("Cancel", ImVec2(90, 0)))
            cbs.exp_state = EXPORT_STATE_NONE;
        right_x -= gap;

        right_x -= 120.0f;
        ImGui::SetCursorScreenPos(ImVec2(right_x, calc.row_top));
        // The primary action: paper fill with ink text on dark.
        ImGui::PushStyleColor(ImGuiCol_Button, r_ui_utils::theme::action_primary);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, r_ui_utils::theme::action_primary_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, r_ui_utils::theme::action_primary_active);
        ImGui::PushStyleColor(ImGuiCol_Text, r_ui_utils::theme::text_on_primary);
        finish_export_clicked = ImGui::Button("Finish export", ImVec2(120, 0));
        ImGui::PopStyleColor(4);
        right_x -= gap;
    }
    else
    {
        right_x -= 80.0f;
        ImGui::SetCursorScreenPos(ImVec2(right_x, calc.row_top));
        export_clicked = ImGui::Button("Export", ImVec2(80, 0));
        right_x -= gap;
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
}

template<typename CONTROL_BAR_BUTTON_CB, typename UPDATE_DATA_CB>
void control_bar_renderer::render_play_live_button(const control_bar_layout& layout, control_bar_state& cbs, const control_bar_calculated_layout& calc,
                                                   const std::string& stream_name, CONTROL_BAR_BUTTON_CB control_bar_button_cb, UPDATE_DATA_CB update_data_cb, bool playing, float& right_x)
{
    bool is_at_live = (cbs.playhead_pos >= timeline_constants::PLAYHEAD_MAX_POSITION);

    // When at live position, show no buttons
    if(is_at_live)
        return;

    const float gap = timeline_constants::ROW_BUTTON_GAP;

    // Placed right to left: Go live, then Play/Pause to its left.
    right_x -= 80.0f;
    float live_button_x = right_x;
    right_x -= gap + 80.0f;
    float play_button_x = right_x;
    right_x -= gap;

    // Playing recorded video: Pause takes Play's slot. Pausing stops the pipe,
    // so on the next frame playing is false and this slot shows Play (= Resume).
    ImGui::SetCursorScreenPos(ImVec2(play_button_x, calc.row_top));
    if(playing)
    {
        if(ImGui::Button("Pause", ImVec2(80, 0)))
            control_bar_button_cb(stream_name, CONTROL_BAR_BUTTON_PAUSE);
    }
    else
    {
        if(ImGui::Button("Play", ImVec2(80, 0)))
            control_bar_button_cb(stream_name, CONTROL_BAR_BUTTON_PLAY);
    }

    ImGui::SetCursorScreenPos(ImVec2(live_button_x, calc.row_top));
    if(ImGui::Button("Go live", ImVec2(80, 0)))
    {
        cbs.live();
        control_bar_button_cb(stream_name, CONTROL_BAR_BUTTON_LIVE);
        update_data_cb(stream_name, cbs);
    }
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

        // Throttle the slider callback to prevent overwhelming the system with HTTP requests
        // Only call the callback every 100ms while dragging
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - cbs.last_slider_callback_time).count();

        bool should_call_callback = (elapsed >= 100);

        // Always call callback when drag ends to ensure final position is set
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            should_call_callback = true;
            cbs.dragging = false;
        }

        if(should_call_callback)
        {
            control_bar_slider_cb(stream_name, cbs.tr.time_in_range(0, 1000, cbs.playhead_pos));
            cbs.last_slider_callback_time = now;
        }
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


    ImGui::PushFont(r_ui_utils::fonts[vision::get_font_key_14()].inter_tight_regular);

    auto jump_minutes = std::chrono::minutes((std::max)(1u, (unsigned)cbs.get_timerange_minutes() / 5));

    // Backward button
    ImGui::PushID("backward");
    auto backward_button_x = ((layout.left + left_box_width) - rs_backward_forward_button_dim) - (calc.text_line_height / 2);
    // Clamp to the bar's left edge so the button stays visible on narrow bars
    // (e.g. the overlay), where the left box is too small to hold it. No-op on
    // the wide main-app bar, where it already sits inside the left box.
    if(backward_button_x < (double)layout.left)
        backward_button_x = (double)layout.left;
    auto backward_button_y = center_line_y - (rs_backward_forward_button_dim / 2);
    ImGui::SetCursorScreenPos(ImVec2((float)backward_button_x, (float)backward_button_y));
    if(ImGui::Button("<", ImVec2((float)rs_backward_forward_button_dim, (float)rs_backward_forward_button_dim)))
    {
        cbs.backward(jump_minutes);
        update_data_cb(stream_name, cbs);
    }
    ImGui::PopID();

    // Forward button
    ImGui::PushID("forward");
    auto forward_button_x = right_box_left + (calc.text_line_height / 2);
    // Clamp to the bar's right edge so the button stays visible on narrow bars.
    double forward_max_x = (double)layout.left + (double)layout.width - rs_backward_forward_button_dim;
    if(forward_button_x > forward_max_x)
        forward_button_x = (float)forward_max_x;
    auto forward_button_y = center_line_y - (rs_backward_forward_button_dim / 2);
    ImGui::SetCursorScreenPos(ImVec2((float)forward_button_x, (float)forward_button_y));
    if(ImGui::Button(">", ImVec2((float)rs_backward_forward_button_dim, (float)rs_backward_forward_button_dim)))
    {
        cbs.forward(jump_minutes);
        update_data_cb(stream_name, cbs);
    }
    ImGui::PopID();

    ImGui::PopFont();
}

} // namespace vision

#endif