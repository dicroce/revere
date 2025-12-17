// Prevent Windows.h from defining min/max macros that conflict with std::min/std::max
#ifdef _WIN32
#define NOMINMAX
#endif

#include "control_bar_renderer.h"
#include "timeline_constants.h"
#include "error_handling.h"
#include "imgui/imgui_internal.h"
#include "r_utils/r_string_utils.h"
#include "r_ui_utils/font_catalog.h"
#include "icon_texture_manager.h"
#include "r_utils/3rdparty/json/json.h"
#include "segment.h"
#include "motion_event.h"
#include "analytics_event.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <set>

using namespace vision;
using namespace std::chrono;

// Helper function to safely get local time (platform-specific)
static std::tm safe_localtime(const std::time_t& time_t_val)
{
    std::tm result;
#ifdef _WIN32
    localtime_s(&result, &time_t_val);
#else
    result = *std::localtime(&time_t_val);
#endif
    return result;
}

control_bar_renderer::control_bar_renderer()
{
}

control_bar_renderer::~control_bar_renderer()
{
}

control_bar_calculated_layout control_bar_renderer::calculate_layout(const control_bar_layout& layout) const
{
    control_bar_calculated_layout calc;
    
    calc.text_line_height = ImGui::GetTextLineHeight();
    calc.center_box_left = layout.left + (layout.width * timeline_constants::LEFT_PANEL_WIDTH_RATIO);
    calc.center_box_width = layout.width * timeline_constants::CENTER_PANEL_WIDTH_RATIO;
    calc.center_box_top = layout.top;
    calc.slider_top = calc.center_box_top + (calc.text_line_height * 2.0f) + timeline_constants::TIMELINE_CONTENT_MARGIN;
    calc.slider_height = timeline_constants::TIMELINE_HEIGHT_MULTIPLIER * calc.text_line_height;
    calc.contents_top = calc.slider_top + timeline_constants::TIMELINE_CONTENT_MARGIN;
    calc.contents_bottom = (calc.slider_top + calc.slider_height) - timeline_constants::TIMELINE_CONTENT_MARGIN;
    calc.event_top = calc.slider_top + (timeline_constants::EVENT_TOP_RATIO * calc.slider_height);
    calc.event_bottom = calc.slider_top + (timeline_constants::EVENT_BOTTOM_RATIO * calc.slider_height);
    
    return calc;
}

float control_bar_renderer::calculate_position_ratio(int64_t time_millis, int64_t duration_millis) const
{
    if (duration_millis <= 0)
        return 0.0f;
    
    double ratio = (double)time_millis / (double)duration_millis;
    return static_cast<float>((std::max)(0.0, (std::min)(1.0, ratio)));
}


bool control_bar_renderer::render_background(ImDrawList* draw_list, const control_bar_layout& layout)
{
    if (!draw_list)
    {
        R_LOG_ERROR("NULL draw_list passed to render_background");
        return false;
    }
    
    return SAFE_EXECUTE_VOID(
        {
            auto calc = calculate_layout(layout);
            
            draw_list->AddRectFilled(
                ImVec2(calc.center_box_left, calc.slider_top),
                ImVec2(calc.center_box_left + calc.center_box_width, calc.slider_top + calc.slider_height),
                ImColor(timeline_constants::colors::TIMELINE_BACKGROUND)
            );
        },
        "rendering timeline background"
    );
}

bool control_bar_renderer::render_segments(ImDrawList* draw_list, const control_bar_state& cbs, const control_bar_layout& layout)
{
    if (!draw_list)
    {
        R_LOG_ERROR("NULL draw_list passed to render_segments");
        return false;
    }
    
    return SAFE_EXECUTE_VOID(
        {
            auto calc = calculate_layout(layout);
            auto bar_duration_millis = cbs.tr.duration_milliseconds();
            
            if (bar_duration_millis <= 0)
            {
                R_LOG_ERROR("Invalid timeline duration: %lld ms", bar_duration_millis);
                return;
            }
            
            for(auto s : cbs.segments)
            {
                // Skip segments with invalid times (start >= end)
                if (s.start >= s.end)
                {
                    continue; // Skip invalid segments
                }
                
                // Clamp segment times to the visible timerange
                auto seg_start = (s.start < cbs.tr.get_start()) ? cbs.tr.get_start() : s.start;
                auto seg_end = (s.end > cbs.tr.get_end()) ? cbs.tr.get_end() : s.end;
                
                // Skip segments completely outside the timerange
                if(seg_start >= cbs.tr.get_end() || seg_end <= cbs.tr.get_start())
                    continue;
                    
                // Calculate milliseconds from the start of the timerange
                int64_t l_millis = duration_cast<milliseconds>(seg_start - cbs.tr.get_start()).count();
                int64_t r_millis = duration_cast<milliseconds>(seg_end - cbs.tr.get_start()).count();
                
                // Ensure we don't have negative values
                l_millis = std::max<int64_t>(0, l_millis);
                r_millis = std::max<int64_t>(0, r_millis);
                
                // Calculate pixel positions
                float l_ratio = calculate_position_ratio(l_millis, bar_duration_millis);
                float r_ratio = calculate_position_ratio(r_millis, bar_duration_millis);
                
                float l = calc.center_box_left + (l_ratio * calc.center_box_width);
                float r = calc.center_box_left + (r_ratio * calc.center_box_width);

                // Validate pixel coordinates
                if (l < 0 || r < 0 || l > 10000 || r > 10000)
                {
                    R_LOG_ERROR("Invalid segment coordinates: l=%f, r=%f", l, r);
                    continue;
                }

                draw_list->AddRectFilled(
                    ImVec2(l, calc.contents_top),
                    ImVec2(r, calc.contents_bottom),
                    ImColor(timeline_constants::colors::RECORDING_SEGMENTS)
                );
            }
        },
        "rendering timeline segments"
    );
}

bool control_bar_renderer::render_motion_events(ImDrawList* draw_list, const control_bar_state& cbs, const control_bar_layout& layout)
{
    if (!draw_list)
    {
        R_LOG_ERROR("NULL draw_list passed to render_motion_events");
        return false;
    }
    
    auto calc = calculate_layout(layout);
    auto bar_duration_millis = cbs.tr.duration_milliseconds();
    
    if (bar_duration_millis <= 0)
    {
        R_LOG_ERROR("Invalid timeline duration for motion events: %lld ms", bar_duration_millis);
        return false;
    }
    
    int rendered_count = 0;
              
    for(auto me : cbs.motion_events)
    {
        // Log motion event times to compare with timeline range
        auto me_start_ms = duration_cast<milliseconds>(me.start.time_since_epoch()).count();
        auto me_end_ms = duration_cast<milliseconds>(me.end.time_since_epoch()).count();
        auto timeline_start_ms = duration_cast<milliseconds>(cbs.tr.get_start().time_since_epoch()).count();
        auto timeline_end_ms = duration_cast<milliseconds>(cbs.tr.get_end().time_since_epoch()).count();
        
        // Only log once per motion event, not every frame
        static std::set<std::pair<int64_t, int64_t>> logged_events;
        auto event_key = std::make_pair(me_start_ms, me_end_ms);
        
        if ((me_start_ms < timeline_start_ms || me_end_ms > timeline_end_ms) && 
            logged_events.find(event_key) == logged_events.end())
        {
            auto extends_beyond = std::max(static_cast<int64_t>(0), me_end_ms - timeline_end_ms);
            R_LOG_INFO("Motion event outside timeline: event=[%lld-%lld], timeline=[%lld-%lld] (extends beyond by %lld ms)", 
                      me_start_ms, me_end_ms, timeline_start_ms, timeline_end_ms, extends_beyond);
            logged_events.insert(event_key);
        }
        
        // Validate motion event times (allow equal start/end times for instantaneous events)
        if (me.start > me.end)
        {
            R_LOG_ERROR("Invalid motion event: start time > end time");
            continue; // Skip invalid events
        }
        
        // Skip events completely outside the timerange (check original times, not clamped)
        if(me.start >= cbs.tr.get_end() || me.end <= cbs.tr.get_start())
            continue;
        
        // Clamp motion event times to the visible timerange
        auto event_start = (me.start < cbs.tr.get_start()) ? cbs.tr.get_start() : me.start;
        auto event_end = (me.end > cbs.tr.get_end()) ? cbs.tr.get_end() : me.end;
        
        // Calculate milliseconds from the start of the timerange
        int64_t l_millis = duration_cast<milliseconds>(event_start - cbs.tr.get_start()).count();
        int64_t r_millis = duration_cast<milliseconds>(event_end - cbs.tr.get_start()).count();
        
        // Ensure we don't have negative values
        l_millis = std::max<int64_t>(0, l_millis);
        r_millis = std::max<int64_t>(0, r_millis);
        
        // Calculate pixel positions
        float l_ratio = calculate_position_ratio(l_millis, bar_duration_millis);
        float r_ratio = calculate_position_ratio(r_millis, bar_duration_millis);
        
        float l = calc.center_box_left + (l_ratio * calc.center_box_width);
        float r = calc.center_box_left + (r_ratio * calc.center_box_width);

        // Check if motion event extends beyond timeline boundaries
        float timeline_right = calc.center_box_left + calc.center_box_width;
        if (r > timeline_right || l < calc.center_box_left)
        {
            R_LOG_INFO("Motion event pixel extends beyond timeline: l=%f, r=%f, timeline_left=%f, timeline_right=%f, l_ratio=%f, r_ratio=%f, event_start_millis=%lld, event_end_millis=%lld", 
                      l, r, calc.center_box_left, timeline_right, l_ratio, r_ratio, l_millis, r_millis);
            
            // Clamp to timeline boundaries
            l = std::max(l, calc.center_box_left);
            r = std::min(r, timeline_right);
        }

        // Ensure minimum width of 1 pixel for visibility
        if (r - l < 1.0f)
        {
            r = l + 1.0f;
        }

        // Validate pixel coordinates
        if (l < 0 || r < 0 || l > 10000 || r > 10000)
        {
            R_LOG_ERROR("Invalid motion event coordinates: l=%f, r=%f", l, r);
            continue;
        }

        // Final bounds check - skip if completely outside timeline
        if (r <= calc.center_box_left || l >= timeline_right)
        {
            R_LOG_INFO("Skipping motion event completely outside timeline bounds");
            continue;
        }

        draw_list->AddRectFilled(
            ImVec2(l, calc.event_top),
            ImVec2(r, calc.event_bottom),
            ImColor(timeline_constants::colors::MOTION_EVENTS)
        );
        rendered_count++;
    }
    
    return true;
}

bool control_bar_renderer::render_analytics_events(ImDrawList* draw_list, const control_bar_state& cbs, const control_bar_layout& layout)
{
    if (!draw_list)
    {
        R_LOG_ERROR("NULL draw_list passed to render_analytics_events");
        return false;
    }
    
    return SAFE_EXECUTE_VOID(
        {
            
            auto calc = calculate_layout(layout);
            auto bar_duration_millis = cbs.tr.duration_milliseconds();
            
            if (bar_duration_millis <= 0)
            {
                R_LOG_ERROR("Invalid timeline duration for analytics events: %lld ms", bar_duration_millis);
                return;
            }
            
            int rendered_count = 0;
            float last_x = -1000.0f; // Initialize to a value that won't cause skipping for the first icon
            for(const auto& event : cbs.analytics_events)
            {
                // Calculate milliseconds from the start of the timerange
                int64_t event_millis = duration_cast<milliseconds>(event.motion_start_time - cbs.tr.get_start()).count();

                // Skip events outside the visible timerange
                if(event_millis < 0 || event_millis > bar_duration_millis)
                    continue;

                // Calculate pixel position
                float position_ratio = calculate_position_ratio(event_millis, bar_duration_millis);
                float x = calc.center_box_left + (position_ratio * calc.center_box_width);

                // Validate pixel coordinates
                if (x < 0 || x > 10000)
                {
                    R_LOG_ERROR("Invalid analytics event coordinate: x=%f", x);
                    continue;
                }

                // Skip drawing if this icon would overlap with the previous one
                const float icon_width = 48.0f;
                if (x - last_x < icon_width)
                {
                    continue; // Skip this icon to prevent overlap
                }

                // First, always draw a colored rectangle as debug indicator
                float center_y = (calc.contents_top + calc.contents_bottom) / 2.0f;
                float rect_size = 12.0f;

                ImU32 rect_color = IM_COL32(255, 255, 255, 255); // White default
                std::string class_name;

                // Get class from first detection
                if (!event.detections.empty())
                {
                    class_name = event.detections[0].class_name;
                    if (class_name == "person")
                    {
                        rect_color = IM_COL32(100, 200, 255, 255); // Light blue
                    }
                    else if (class_name == "car" || class_name == "vehicle")
                    {
                        rect_color = IM_COL32(255, 150, 100, 255); // Light orange
                    }
                }
                
                // Draw colored rectangle as background/debug indicator
                draw_list->AddRectFilled(
                    ImVec2(x - rect_size/2, center_y - rect_size/2),
                    ImVec2(x + rect_size/2, center_y + rect_size/2),
                    rect_color
                );
                
                // Try to draw icon on top
                GLuint texture_id = get_icon_texture_id(class_name);

                if (texture_id != 0)
                {
                    // Draw the icon using the texture
                    float icon_size = 48.0f; // Match the size we created the textures at
                    float icon_x = x - icon_size / 2.0f;
                    float icon_y = center_y - icon_size / 2.0f;

                    // Draw background rectangle matching recording segments color
                    ImU32 bg_color = IM_COL32(73, 106, 129, 255); // RGB(0.286, 0.415, 0.505) converted to 0-255
                    draw_list->AddRectFilled(
                        ImVec2(icon_x, icon_y),
                        ImVec2(icon_x + icon_size, icon_y + icon_size),
                        bg_color
                    );

                    // Convert GLuint to ImTextureID
                    ImTextureID imgui_texture_id = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture_id));

                    // Draw the icon on top of the background
                    draw_list->AddImage(
                        imgui_texture_id,
                        ImVec2(icon_x, icon_y),
                        ImVec2(icon_x + icon_size, icon_y + icon_size),
                        ImVec2(0, 0),
                        ImVec2(1, 1),
                        IM_COL32_WHITE
                    );
                }
                
                // Update last_x position after successfully drawing
                last_x = x;
                rendered_count++;
            }
        },
        "rendering analytics events"
    );
}

bool control_bar_renderer::render_playhead(ImDrawList* draw_list, const control_bar_state& cbs, const control_bar_layout& layout, bool playing)
{
    // Playhead is now visible during playback to show current position
        
    if (!draw_list)
    {
        R_LOG_ERROR("NULL draw_list passed to render_playhead");
        return false;
    }
    
    // Validate playhead position
    if (!state_validate::is_valid_playhead_position(cbs.playhead_pos))
    {
        return false; // Already logged in validation
    }
    
    return SAFE_EXECUTE_VOID(
        {
            auto calc = calculate_layout(layout);
            
            // Draw playhead
            double playhead_x;
            bool is_at_live = (cbs.playhead_pos >= timeline_constants::PLAYHEAD_MAX_POSITION);
            
            if (is_at_live && playing) {
                // When playing live, fix playhead to right edge
                playhead_x = (calc.center_box_left + calc.center_box_width) - cbs.playhead_width;
            } else {
                // Normal playhead positioning based on playhead_pos
                playhead_x = (calc.center_box_left + (((double)cbs.playhead_pos / 1000.0) * (double)calc.center_box_width)) - (cbs.playhead_width / 2);
            }
            
            auto playhead_top = calc.slider_top;
            
            // Validate coordinates
            if (playhead_x < 0 || playhead_x > 10000 || playhead_top < 0 || playhead_top > 10000)
            {
                R_LOG_ERROR("Invalid playhead coordinates: x=%f, top=%f", playhead_x, playhead_top);
                return;
            }
            
            draw_list->AddRectFilled(
                ImVec2((float)playhead_x, (float)playhead_top),
                ImVec2((float)(playhead_x + cbs.playhead_width), (float)(playhead_top + calc.slider_height)),
                ImColor(timeline_constants::colors::PLAYHEAD)
            );
            
            // Draw playhead text
            ImGui::PushFont(r_ui_utils::fonts[timeline_constants::fonts::CONTROLS].roboto_regular);
            auto playhead_pos_s = cbs.playhead_pos_s();
            auto text_size = ImGui::CalcTextSize(playhead_pos_s.c_str());
            auto playhead_text_x = playhead_x - (text_size.x / 2);
            
            if(playhead_text_x < calc.center_box_left)
                playhead_text_x = calc.center_box_left;
                
            auto center_box_right = calc.center_box_left + calc.center_box_width;
            if(playhead_x > (center_box_right - text_size.x))
                playhead_text_x = (center_box_right - text_size.x);
                
            auto playhead_text_y = (playhead_top + (calc.text_line_height * 3));
            ImGui::SetCursorScreenPos(ImVec2((float)playhead_text_x, (float)playhead_text_y));
            ImGui::Text("%s", playhead_pos_s.c_str());
            ImGui::PopFont();
        },
        "rendering playhead"
    );
}

bool control_bar_renderer::render_tick_marks(ImDrawList* draw_list, const control_bar_state& cbs, const control_bar_layout& layout)
{
    if (!draw_list)
    {
        R_LOG_ERROR("NULL draw_list passed to render_tick_marks");
        return false;
    }
    
    return SAFE_EXECUTE_VOID(
        {
            auto calc = calculate_layout(layout);
            auto bar_duration_millis = cbs.tr.duration_milliseconds();
            
            if (bar_duration_millis <= 0)
            {
                R_LOG_ERROR("Invalid timeline duration for tick marks: %lld ms", bar_duration_millis);
                return;
            }
            
            // Get the start and end times of the timeline
            auto start_time = cbs.tr.get_start();
            auto end_time = cbs.tr.get_end();
            
            // Convert to time_t for easier manipulation
            auto start_time_t = system_clock::to_time_t(start_time);
            
            // Get local time and round to next 5-minute mark
            std::tm start_tm = safe_localtime(start_time_t);
            
            // Round up to next 5-minute mark
            int minutes_to_next_5 = 5 - (start_tm.tm_min % 5);
            if (minutes_to_next_5 == 5) minutes_to_next_5 = 0;
            
            // Create first tick time
            std::tm tick_tm = start_tm;
            tick_tm.tm_min += minutes_to_next_5;
            tick_tm.tm_sec = 0;
            std::mktime(&tick_tm); // Normalize the time structure
            
            // Draw tick marks at every 5-minute interval
            while (true)
            {
                auto tick_time_t = std::mktime(&tick_tm);
                auto tick_time = system_clock::from_time_t(tick_time_t);
                
                // Check if we've passed the end time
                if (tick_time > end_time)
                    break;
                
                // Calculate position on timeline
                auto millis_from_start = duration_cast<milliseconds>(tick_time - start_time).count();
                float tick_ratio = calculate_position_ratio(millis_from_start, bar_duration_millis);
                float tick_x = calc.center_box_left + (tick_ratio * calc.center_box_width);
                
                // Draw tick mark (vertical line over the content area)
                draw_list->AddLine(
                    ImVec2(tick_x, calc.contents_top),
                    ImVec2(tick_x, calc.contents_bottom),
                    ImColor(0.6f, 0.65f, 0.7f, 0.55f),  // Lighter bluish-gray with transparency
                    1.0f
                );
                
                // Move to next 5-minute mark
                tick_tm.tm_min += 5;
                std::mktime(&tick_tm); // Normalize the time structure
            }
        },
        "rendering tick marks"
    );
}

void control_bar_renderer::render_timerange_text(const control_bar_layout& layout, const control_bar_state& cbs, const control_bar_calculated_layout& calc)
{
    auto top_line_top = calc.center_box_top + timeline_constants::TOP_BUTTON_OFFSET;

    ImGui::PushFont(r_ui_utils::fonts[vision::get_font_key_16()].roboto_regular);
    ImGui::SetCursorScreenPos(ImVec2(calc.center_box_left, (float)top_line_top));
    auto timerange_s = r_utils::r_string_utils::format("%u minute time range", cbs.get_timerange_minutes());
    ImGui::Text("%s", timerange_s.c_str());
    ImGui::PopFont();
}

// Template implementation must be in header or explicitly instantiated
template<typename CONTROL_BAR_SLIDER_CB, typename CONTROL_BAR_BUTTON_CB, typename UPDATE_DATA_CB, typename EXPORT_CB>
void control_bar_renderer::render(
    const control_bar_layout& layout,
    bool show_motion_events,
    control_bar_state& cbs,
    CONTROL_BAR_SLIDER_CB control_bar_slider_cb,
    CONTROL_BAR_BUTTON_CB control_bar_button_cb,
    UPDATE_DATA_CB update_data_cb,
    EXPORT_CB export_cb,
    bool playing
)
{
    ImGui::SetNextWindowPos(ImVec2(layout.left, layout.top));
    ImGui::SetNextWindowSize(ImVec2(layout.width, layout.height));
    ImGui::Begin("##control_bar", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Render components
    render_background(draw_list, layout);
    render_segments(draw_list, cbs, layout);
    
    if(show_motion_events)
        render_motion_events(draw_list, cbs, layout);
    
    // Render analytics events (people and cars)
    render_analytics_events(draw_list, cbs, layout);
        
    render_playhead(draw_list, cbs, layout, playing);
    
    // TODO: Extract button rendering and input handling
    // For now, keep the existing button and input code here
    
    ImGui::End();
}

// Explicit template instantiation for the specific callback types used
#include "pipeline_host.h"
template void control_bar_renderer::render<
    std::function<void(const std::string&, const std::chrono::system_clock::time_point&)>,
    std::function<void(const std::string&, control_bar_button_type)>,
    std::function<void(control_bar_state&)>,
    std::function<void(const std::chrono::system_clock::time_point&, const std::chrono::system_clock::time_point&, control_bar_state&)>
>(
    const control_bar_layout& layout,
    bool show_motion_events,
    control_bar_state& cbs,
    std::function<void(const std::string&, const std::chrono::system_clock::time_point&)> control_bar_slider_cb,
    std::function<void(const std::string&, control_bar_button_type)> control_bar_button_cb,
    std::function<void(control_bar_state&)> update_data_cb,
    std::function<void(const std::chrono::system_clock::time_point&, const std::chrono::system_clock::time_point&, control_bar_state&)> export_cb,
    bool playing
);