#ifndef r_utils_r_restart_h
#define r_utils_r_restart_h

#include "r_utils/r_macro.h"
#include <chrono>
#include <string>
#include <vector>

namespace r_utils
{

// Restarting the running program: the caller asks for a restart, then exits the
// way it always does, and a new copy starts once this one has gone. The new copy
// is this program's executable with this process's arguments (less drop_args),
// started now but held in wait_for_predecessor() until this process exits, so it
// never meets this one's single-instance lock, ports or files.
//
// Headless on Linux, nothing is started: Revere runs there under systemd, whose
// unit (Restart=always) starts it again when it exits. Under an AppImage the
// AppImage file is started rather than the executable inside it, which is
// unmounted when this process exits.
namespace r_restart
{

// Starts the new copy; the caller must then exit (normally: shutting down
// cleanly is what lets the new copy run). False, with why, if no new copy could
// be started, in which case the caller should keep running.
R_API bool restart(bool headless, const std::vector<std::string>& drop_args, std::string& why);

// Call first thing in main. In a copy restart() started, waits until the process
// that started it has exited (up to timeout, after which it carries on and meets
// whatever lock that process still holds); otherwise returns at once. True if
// this process was started by restart().
R_API bool wait_for_predecessor(std::chrono::milliseconds timeout = std::chrono::seconds(60));

// For tests: restart()'s Windows command line from args (argv[0] first), quoted
// so CommandLineToArgvW and the C runtime read back the same args.
R_API std::string windows_command_line(const std::vector<std::string>& args);

}

}

#endif
