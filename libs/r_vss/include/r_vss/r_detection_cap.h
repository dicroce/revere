#ifndef __r_vss_r_detection_cap_h
#define __r_vss_r_detection_cap_h

#include "r_utils/r_macro.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace r_vss
{

// One detection of a motion event, as the cap sees it.
struct r_capped_detection
{
    int64_t ts {0};          // its frame's ts; detections sharing a ts are one frame
    std::string class_name;
    float confidence {0};
    float area {0};          // box area, any consistent units; 0 if unknown
};

// Detections kept in an event's analytics record. Each is about 150 bytes of
// JSON, so 300 is about 45 KB, well inside a 512 KB metadata block; uncapped, a
// long busy event outgrows the block and the record fails to write.
static constexpr size_t MAX_EVENT_DETECTIONS = 300;

// Frames from the start of the event that are always kept: the event's first
// images are taken from its earliest distinct detection timestamps.
static constexpr size_t EVENT_DETECTIONS_FIRST_FRAMES = 6;

// The best-scoring frames that are always kept, whatever their size: frames
// for image analysis are chosen as the best few by the same score.
static constexpr size_t EVENT_DETECTIONS_BEST_FRAMES = 6;

// Which detections to keep when an event has more than max_detections, as one
// flag per detection. Frames (detections sharing a ts) are kept or dropped
// whole, so per-frame counts stay exact. A frame scores as its best detection:
// area x confidence, or confidence without an area. Always kept: the first
// EVENT_DETECTIONS_FIRST_FRAMES frames, the last frame, the
// EVENT_DETECTIONS_BEST_FRAMES best-scoring frames, the frame of the most
// confident detection, each class's best-scoring frame and the frame with the
// most people. Then the best-scoring of the rest, each that still fits within
// max_detections. All true when nothing needs dropping.
R_API std::vector<bool> cap_event_detections(const std::vector<r_capped_detection>& detections,
                                             size_t max_detections = MAX_EVENT_DETECTIONS);

}

#endif
