#ifndef __revere_tray_companion_h
#define __revere_tray_companion_h

#include <string>

namespace revere
{

// `revere --tray`: a tray icon for a Revere that is already running somewhere
// else on this machine, usually the headless service. It does no recording and
// takes no part of Revere's single-instance lock; it only reads the local API
// (http://127.0.0.1:8088) to show status, and opens the web UI.
// Returns the process exit code.
int run_tray_companion(const std::string& icon_path);

}

#endif
