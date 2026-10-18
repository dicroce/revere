
#ifndef __r_ui_utils_section_state_h
#define __r_ui_utils_section_state_h

#include "r_utils/r_macro.h"
#include "r_utils/3rdparty/json/json.h"
#include <map>
#include <string>

namespace r_ui_utils
{

// Which collapsible sections of a window are open, by a stable key per section
// (not its label, which can be reworded or translated). A section with no
// entry uses the caller's default.
using sections_open = std::map<std::string, bool>;

// Reads saved sections. Anything that isn't a JSON object of booleans is
// skipped entry by entry, so a bad entry only loses that section's state.
R_API sections_open parse_sections_open(const nlohmann::json& j);

R_API nlohmann::json sections_open_to_json(const sections_open& open);

// The saved state for key, or def if there's none.
R_API bool section_open(const sections_open& open, const std::string& key, bool def);

}

#endif
