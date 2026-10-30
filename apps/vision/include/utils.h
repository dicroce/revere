
#ifndef __vision_utils_h
#define __vision_utils_h

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include "segment.h"
#include "motion_event.h"
#include "configure_state.h"

namespace vision
{

std::string top_dir();
std::string sub_dir(const std::string& subdir);
std::string join_path(const std::string& path, const std::string& fileName);

// Write a decoded frame (NV12 or BGRA, as posted to pipeline_host) as a PNG to
// <data>/snapshots/<camera>_<YYYY-MM-DD_HH-MM-SS>.png and return the path.
// original_h is the camera's native height (picks BT.601 vs BT.709 for NV12).
// Throws on failure.
std::string save_snapshot_png(const std::string& camera_label, const uint8_t* data, bool nv12, uint16_t w, uint16_t h, uint16_t original_h);

}

#endif
