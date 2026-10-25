#ifndef __r_vss_r_event_timing_h
#define __r_vss_r_event_timing_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <string>
#include <vector>

namespace r_vss
{

// Gateway wall clock (system_clock) in epoch milliseconds: the clock the
// motion event timing fields below and the detection bus's posted_at use.
R_API int64_t wall_clock_ms();

// One key frame YOLO ran on during a motion event.
struct r_yolo_look
{
    int64_t ts {0};          // the frame's ts, epoch ms
    int64_t inferred_at {0}; // wall clock when inference finished
    int n_dets {0};          // detections the frame added to the event
};

// When a motion event's stages happened, by the gateway wall clock. 0 is
// unknown, e.g. the plugin's queue was full when the START was handed over.
struct r_event_timing
{
    int64_t confirmed_at {0}; // the START was handed to the motion plugins
    int64_t ended_at {0};     // the END was handed to the motion plugins
    std::vector<r_yolo_look> looks;
    size_t looks_dropped {0};  // looks left out of the middle by add_look
};

// Looks kept per event: the first half and the most recent half. A look is
// about 66 bytes of JSON, so 300 is about 20 KB, well inside a 512 KB metadata
// block; an uncapped list (a look a second for hours) would outgrow the block
// and the record would fail to write.
static constexpr size_t MAX_EVENT_LOOKS = 300;

// Adds a look to the event, dropping the oldest of the most recent half once
// MAX_EVENT_LOOKS are kept, and counting it in looks_dropped.
R_API void add_look(r_event_timing& timing, const r_yolo_look& look);

// The timing as JSON members for the analytics object, each with a leading
// ", ": confirmed_at, ended_at, written_at, looks [{ts, inferred_at, n_dets}]
// and looks_dropped. Unknown (<= 0) times, an empty looks list and a zero
// looks_dropped are left out.
R_API std::string event_timing_json_members(const r_event_timing& timing, int64_t written_at);

}

#endif
