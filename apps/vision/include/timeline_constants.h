#ifndef __vision_timeline_constants_h
#define __vision_timeline_constants_h

#include "imgui/imgui.h"
#include "r_ui_utils/theme.h"
#include <chrono>

namespace vision
{

namespace timeline_constants
{
    // Update intervals
    constexpr auto SEGMENT_UPDATE_INTERVAL = std::chrono::milliseconds(5000);
    constexpr auto UI_UPDATE_INTERVAL = std::chrono::seconds(5);
    
    // Timeline behavior
    constexpr int PLAYHEAD_LIVE_THRESHOLD = 999;
    constexpr int PLAYHEAD_MAX_POSITION = 1000;
    constexpr int PLAYHEAD_MIN_POSITION = 0;
    constexpr int PLAYHEAD_DEFAULT_WIDTH = 3;
    constexpr int TIMERANGE_MIN_MINUTES = 10;
    constexpr int TIMERANGE_MAX_MINUTES = 480;

    // The time range dropdown's choices.
    struct timerange_choice { int minutes; const char* label; };
    constexpr timerange_choice TIMERANGE_CHOICES[] = {
        {10, "10 min"}, {20, "20 min"}, {30, "30 min"},
        {60, "1 hour"}, {120, "2 hours"}, {240, "4 hours"}, {480, "8 hours"}
    };
    
    // Layout constants
    constexpr float LEFT_PANEL_WIDTH_RATIO = 0.04f;  // 4% for left button area
    constexpr float CENTER_PANEL_WIDTH_RATIO = 0.92f;  // 92% for timeline
    constexpr float TIMELINE_HEIGHT_MULTIPLIER = 2.0f;
    constexpr float TIMELINE_CONTENT_MARGIN = 5.0f;
    constexpr float EVENT_TOP_RATIO = 0.25f;
    constexpr float EVENT_BOTTOM_RATIO = 0.75f;
    constexpr float TOP_BUTTON_OFFSET = 6.0f;   // bar top to the control row
    constexpr float ROW_GAP = 6.0f;             // control row to the timeline strip
    constexpr float BOTTOM_MARGIN = 6.0f;       // timeline strip to the bar bottom
    constexpr float ROW_BUTTON_GAP = 10.0f;     // between buttons in the control row
    
    // Export overlay
    constexpr float EXPORT_TICK_WIDTH = 4.0f; // 2 pixels on each side
    constexpr int EXPORT_PLAYHEAD_ADVANCE = 50;
    
    // Button sizes
    constexpr float TIMERANGE_BUTTON_SIZE_RATIO = 0.75f;
    constexpr float LIVE_BUTTON_WIDTH = 60.0f;
    constexpr float PLAY_BUTTON_OFFSET = 120.0f;
    constexpr float EXPORT_BUTTON_OFFSET = 300.0f;
    constexpr float EXPORT_CANCEL_BUTTON_OFFSET = 200.0f;
    constexpr float NAVIGATION_BUTTON_SIZE_RATIO = 1.25f;
    
    // Colors (Revere Design System tokens, r_ui_utils/theme.h)
    namespace colors
    {
        // Timeline track
        constexpr ImVec4 TIMELINE_BACKGROUND = r_ui_utils::theme::neutral_800;

        // Recorded footage: neutral, so motion and detection colors stand out on it
        constexpr ImVec4 RECORDING_SEGMENTS = r_ui_utils::theme::neutral_600;

        // Motion with no class: the motion token at 60%
        constexpr ImVec4 MOTION_EVENTS = r_ui_utils::theme::motion;

        // Playhead and export start marker: accent
        constexpr ImVec4 PLAYHEAD = r_ui_utils::theme::accent;
        constexpr ImVec4 EXPORT_MARKER = r_ui_utils::theme::accent;

        // "Drag camera here..." hint
        constexpr ImVec4 PLACEHOLDER_TEXT = r_ui_utils::theme::text_tertiary;
    }
    
    // Font sizes (matching existing font catalog)
    namespace fonts
    {
        constexpr const char* MAIN_UI = "24.00";
        constexpr const char* SIDEBAR_TITLE = "24.00";
        constexpr const char* CONTROLS = "16.00";
        constexpr const char* STATUS_BAR = "18.00";
        constexpr const char* NAVIGATION = "14.00";
    }
    
    // Network and timing
    constexpr int SOCKET_TIMEOUT_MS = 10000;
    constexpr int COMMUNICATION_RETRY_INTERVAL_US = 1000000; // 1 second
    
    // Window and UI
    constexpr int DEFAULT_WINDOW_WIDTH = 1280;
    constexpr int DEFAULT_WINDOW_HEIGHT = 720;
    constexpr float SDL_EVENT_TIMEOUT_SEC = 0.1f;
    
    // Video positioning (from our earlier fix)
    constexpr float VIDEO_ASPECT_RATIO_TOLERANCE = 0.01f;
}

} // namespace vision

#endif