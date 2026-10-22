
#ifndef __revere_vision_launch_h
#define __revere_vision_launch_h

#ifdef IS_MACOS
#include <string>
#include <vector>

// The app bundle's executable (from its Info.plist) if it exists and can be
// run, else "". Read fresh each call: NSBundle would cache a bundle built
// after the first look.
std::string app_bundle_executable(const std::string& app_path);

// vision.app next to revere.app (exe_dir is revere.app/Contents/MacOS: a build
// tree, or an install), else /Applications/Vision.app; "" if neither can run.
// Only a bundle with its executable in it counts: building the revere target
// alone leaves an empty vision.app behind, which `open` refuses.
std::string find_vision_app(const std::string& exe_dir);

// Runs `open -n <app> [--args <args>]` without a shell (so nothing needs
// quoting) and without inheriting revere's fds. A detached thread waits for
// open and logs its failure with the exit status and what it printed, so the
// UI doesn't wait on LaunchServices.
// on_exit, if set, gets open's exit code (-1 if it didn't exit normally) on
// that thread. False if open couldn't be started (logged).
bool open_app(const std::string& app, const std::vector<std::string>& args,
              void (*on_exit)(int code) = nullptr);
#endif

#endif
