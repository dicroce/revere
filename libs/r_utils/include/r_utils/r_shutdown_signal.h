
#ifndef r_utils_r_shutdown_signal_h
#define r_utils_r_shutdown_signal_h

#include "r_utils/r_macro.h"
#include <string>

namespace r_utils
{

// For a process with no window that runs until it's told to stop.
namespace r_shutdown_signal
{

// Call on the main thread before any other thread starts.
// Linux/macOS: SIGINT and SIGTERM are blocked in every thread from then on, so
// only wait() takes them.
// Windows: attaches to the console that started this process, if any (a GUI
// program has none of its own), so Ctrl+C reaches it, and creates the named
// event "Local\<windows_event_name>", which another process can set to stop it.
R_API void arm(const std::string& windows_event_name);

// Blocks until a stop arrives. Call after arm().
R_API void wait();

}

}

#endif
