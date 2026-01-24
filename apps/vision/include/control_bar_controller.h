#ifndef __vision_control_bar_controller_h
#define __vision_control_bar_controller_h

#include "control_bar.h"
#include "control_bar_renderer.h"
#include "timeline_constants.h"
#include <chrono>
#include <functional>

namespace vision
{

// Controller class that manages control bar state and coordinates rendering
class control_bar_controller
{
public:
    control_bar_controller();
    ~control_bar_controller();

    // Main update and render function
    template<typename CONTROL_BAR_SLIDER_CB, typename CONTROL_BAR_BUTTON_CB, typename UPDATE_DATA_CB, typename EXPORT_CB>
    void update_and_render(
        const control_bar_layout& layout,
        bool show_motion_events,
        control_bar_state& cbs,
        CONTROL_BAR_SLIDER_CB control_bar_slider_cb,
        CONTROL_BAR_BUTTON_CB control_bar_button_cb,
        UPDATE_DATA_CB update_data_cb,
        EXPORT_CB export_cb,
        bool playing
    );

private:
    // State management
    void handle_initialization(control_bar_state& cbs, const control_bar_layout& layout,
                              std::function<void(const std::string&, const std::chrono::system_clock::time_point&)> control_bar_slider_cb,
                              std::function<void(const std::string&, control_bar_button_type)> control_bar_button_cb);
    
    void handle_periodic_updates(control_bar_state& cbs, 
                                std::function<void(control_bar_state&)> update_data_cb);
    
    void render_export_overlay(ImDrawList* draw_list, const control_bar_state& cbs, const control_bar_layout& layout);
    
    // Renderer instance
    control_bar_renderer renderer;
    
    // Timing state
    std::chrono::steady_clock::time_point last_timeline_update;
};

} // namespace vision

#endif