
#ifndef __vision_macos_activation_h
#define __vision_macos_activation_h

#ifdef IS_MACOS
#include <cstdint>
// Lets the running process pid take focus when it next asks (macOS 14+
// "cooperative activation": an app only comes to the front if the active app
// yields to it). Does nothing on older macOS or if pid isn't running.
void yield_activation_to_pid(int64_t pid);
#endif

#endif
