
#ifndef r_utils_r_desktop_h
#define r_utils_r_desktop_h

#include "r_utils/r_macro.h"
#include <string>

namespace r_utils
{

namespace r_desktop
{

// Opens a URL, file or folder the way the desktop would: the default browser
// for a URL, a file manager for a folder. Windows: ShellExecute; macOS: open;
// Linux: xdg-open (which works through the XDG desktop portal in a snap).
//
// The opener gets target as its one argument, never through a shell, and isn't
// waited for: with no browser running, xdg-open runs the one it starts in the
// foreground and returns only when that browser quits. A failure it reports
// later is logged. Throws if the opener can't be started.
R_API void open(const std::string& target);

}

}

#endif
