
#ifndef r_utils_r_startup_h
#define r_utils_r_startup_h

#include "r_utils/r_macro.h"
#include <string>

namespace r_utils
{

namespace r_startup
{

/// @brief Enable or disable application auto-start on system login
/// @param enable true to enable auto-start, false to disable
/// @param app_name Name of the application (used for registry/config entries)
/// @param exe_path Full path to the executable
/// @param args Optional command line arguments (e.g., "--start_minimized")
/// @return true if operation succeeded, false otherwise
R_API bool set_autostart(bool enable, const std::string& app_name, const std::string& exe_path, const std::string& args = "");

/// @brief Check if application is currently set to auto-start
/// @param app_name Name of the application
/// @return true if auto-start is enabled, false otherwise
R_API bool is_autostart_enabled(const std::string& app_name);

}

}

#endif
