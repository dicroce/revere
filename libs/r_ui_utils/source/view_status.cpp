#include "r_ui_utils/view_status.h"

using namespace r_ui_utils;
using namespace std::chrono;

view_stage r_ui_utils::view_stage_now(const view_signals& s, steady_clock::time_point now)
{
    if(!s.live)
        return view_stage::none;
    if(s.restarting)
        return view_stage::reconnecting;
    if(!s.presented)
        return s.got_sample ? view_stage::waiting_for_picture : view_stage::connecting;
    if(now - s.last_present >= VIEW_STALL_AFTER)
        return view_stage::waiting_for_video;
    return view_stage::none;
}

// Plain dots: the UI fonts carry Latin-1 only, with no ellipsis glyph.
const char* r_ui_utils::view_stage_text(view_stage stage)
{
    switch(stage)
    {
        case view_stage::connecting: return "Connecting to camera...";
        case view_stage::waiting_for_picture: return "Waiting for the first picture...";
        case view_stage::waiting_for_video: return "Waiting for video...";
        case view_stage::reconnecting: return "Reconnecting...";
        case view_stage::none: break;
    }
    return "";
}

view_stage view_status::update(const view_signals& s, steady_clock::time_point now)
{
    auto next = view_stage_now(s, now);
    if(next == _shown)
        return _shown;
    if(_shown != view_stage::none && now - _shown_at < VIEW_LABEL_MIN_SHOW)
        return _shown;
    _shown = next;
    _shown_at = now;
    return _shown;
}
