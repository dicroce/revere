#ifndef __vision_timeline_constants_h
#define __vision_timeline_constants_h

#include "imgui/imgui.h"
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
    
    // Layout constants
    constexpr float LEFT_PANEL_WIDTH_RATIO = 0.04f;  // 4% for left button area
    constexpr float CENTER_PANEL_WIDTH_RATIO = 0.92f;  // 92% for timeline
    constexpr float TIMELINE_HEIGHT_MULTIPLIER = 3.0f;
    constexpr float TIMELINE_CONTENT_MARGIN = 5.0f;
    constexpr float EVENT_TOP_RATIO = 0.25f;
    constexpr float EVENT_BOTTOM_RATIO = 0.75f;
    constexpr float TOP_BUTTON_OFFSET = 10.0f;  // Offset for top row buttons
    
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
    
    // Colors
    namespace colors
    {
        // Timeline background (dark gray)
        constexpr ImVec4 TIMELINE_BACKGROUND{0.2f, 0.2f, 0.2f, 1.0f};
        
        // Recording segments (blue)
        constexpr ImVec4 RECORDING_SEGMENTS{0.286f, 0.415f, 0.505f, 1.0f}; // #496a81
        
        // Motion events (lighter blue)
        constexpr ImVec4 MOTION_EVENTS{0.400f, 0.600f, 0.607f, 1.0f}; // #66999b
        
        // Playhead (green)
        constexpr ImVec4 PLAYHEAD{0.0f, 1.0f, 0.0f, 1.0f};
        
        // Export marker (bright green)
        constexpr ImVec4 EXPORT_MARKER{0.0f, 1.0f, 0.0f, 1.0f};
        
        // UI elements
        constexpr ImVec4 EXPORT_BUTTON_ACTIVE{0.3671f, 0.7695f, 0.5078f, 1.0f}; // HSV converted
        constexpr ImVec4 CANCEL_BUTTON{0.0f, 0.6f, 0.6f, 1.0f};
        
        // Sidebar colors
        constexpr ImU32 SIDEBAR_HEADER_HOVERED = IM_COL32(20, 52, 77, 200);
        constexpr ImU32 SIDEBAR_NAV_HIGHLIGHT = IM_COL32(255, 0, 0, 200);
        constexpr ImU32 SIDEBAR_HEADER_ACTIVE = IM_COL32(25, 47, 63, 200);
        constexpr ImU32 SIDEBAR_HEADER = IM_COL32(49, 55, 62, 200);
        constexpr ImU32 SIDEBAR_DIVIDER = IM_COL32(40, 40, 40, 200);
        
        // Text colors
        constexpr ImU32 PLACEHOLDER_TEXT = IM_COL32(128, 128, 128, 128);
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
    constexpr float GLFW_EVENT_TIMEOUT = 0.1f;
    
    // Video positioning (from our earlier fix)
    constexpr float VIDEO_ASPECT_RATIO_TOLERANCE = 0.01f;
}

} // namespace vision

#endif