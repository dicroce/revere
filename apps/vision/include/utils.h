
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

}

#endif
