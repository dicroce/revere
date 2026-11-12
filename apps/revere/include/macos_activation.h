
#ifndef __revere_macos_activation_h
#define __revere_macos_activation_h

#ifdef IS_MACOS
// Lets the app with this bundle id take focus when it next asks (macOS 14+
// "cooperative activation": an app only comes to the front if the active app
// yields to it). Call just before launching it. Does nothing on older macOS,
// where launched apps activate anyway.
void yield_activation_to(const char* bundle_id);

struct SDL_Window;
// Whether the window can't be seen: ordered out, miniaturised, hidden with the app (Cmd-H), on another Space or fully
// covered by other windows. SDL has no event or flag for these. False if the window can't be asked about.
bool macos_window_unseen(SDL_Window* window);
#endif

#endif
