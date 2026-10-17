
#ifndef __revere_macos_activation_h
#define __revere_macos_activation_h

#ifdef IS_MACOS
// Lets the app with this bundle id take focus when it next asks (macOS 14+
// "cooperative activation": an app only comes to the front if the active app
// yields to it). Call just before launching it. Does nothing on older macOS,
// where launched apps activate anyway.
void yield_activation_to(const char* bundle_id);
#endif

#endif
