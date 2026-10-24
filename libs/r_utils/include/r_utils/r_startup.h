
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

/// What a Linux autostart entry is launched from, as found in the environment
/// (empty when unset).
struct linux_launch_env
{
    std::string snap;        // $SNAP
    std::string snap_name;   // $SNAP_NAME
    std::string flatpak_id;  // $FLATPAK_ID
    std::string appimage;    // $APPIMAGE: the AppImage file itself
};

/// @brief The Exec= value of a Linux autostart .desktop entry
/// @details Snap: /snap/bin/<name>. Flatpak: its app id. AppImage: $APPIMAGE,
/// the AppImage file. The running executable is inside the AppImage's temporary
/// mount (/tmp/.mount_XXXX), which is gone after a reboot. Otherwise exe_path.
/// The path is quoted for the Exec key when it needs to be.
R_API std::string linux_autostart_exec(const linux_launch_env& env, const std::string& exe_path, const std::string& args);

}

}

#endif
