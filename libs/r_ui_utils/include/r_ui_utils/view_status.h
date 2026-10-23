#ifndef __r_ui_utils_view_status_h
#define __r_ui_utils_view_status_h

#include "r_utils/r_macro.h"
#include <chrono>

namespace r_ui_utils
{

// What a video pane says over its picture while live video starts or stalls.
// A pane that is playing says nothing.
enum class view_stage
{
    none,
    connecting,          // live started; nothing from the camera yet
    waiting_for_picture, // video is arriving; no picture decoded yet
    waiting_for_video,   // live was playing and its picture has been still a while
    reconnecting         // the stream died and is being started again
};

// What the pane knows about its live stream, sampled once per frame.
struct view_signals
{
    bool live {true};        // following live (not playback or a still from the timeline)
    bool restarting {false}; // a dead stream was restarted and no live picture has shown since
    bool got_sample {false}; // the source has delivered video since live started
    bool presented {false};  // a live picture has shown since live started
    std::chrono::steady_clock::time_point last_present {}; // when the latest live picture showed
};

// A routine gap while the decoder waits for the next keyframe lasts about 2 s,
// so a still picture is only called out after this long.
constexpr std::chrono::milliseconds VIEW_STALL_AFTER {3000};

// Once shown, a label stays at least this long, so it doesn't flicker.
constexpr std::chrono::milliseconds VIEW_LABEL_MIN_SHOW {1000};

// The stage the signals call for right now, with no smoothing.
R_API view_stage view_stage_now(const view_signals& s, std::chrono::steady_clock::time_point now);

// The words shown for a stage; empty for none.
R_API const char* view_stage_text(view_stage stage);

// The stage a pane shows: view_stage_now, except that a label, once shown,
// stays for at least VIEW_LABEL_MIN_SHOW.
class R_API view_status final
{
public:
    view_stage update(const view_signals& s, std::chrono::steady_clock::time_point now);
    view_stage shown() const { return _shown; }

private:
    view_stage _shown {view_stage::none};
    std::chrono::steady_clock::time_point _shown_at {};
};

}

#endif
