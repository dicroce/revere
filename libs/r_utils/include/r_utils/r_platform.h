
#ifndef r_utils_r_platform_h
#define r_utils_r_platform_h

// Compile-time-resolved OS query helpers. The binary is built for one OS, so
// these are constant for a given build — but a runtime-callable API is handy for
// guarding platform-specific features (e.g. systemd service install), logging,
// or reporting the host OS, without sprinkling #ifdef IS_LINUX through call
// sites. The IS_WINDOWS / IS_LINUX / IS_MACOS macros come from settings.cmake.

namespace r_utils
{
namespace r_platform
{

inline bool is_windows()
{
#ifdef IS_WINDOWS
    return true;
#else
    return false;
#endif
}

inline bool is_linux()
{
#ifdef IS_LINUX
    return true;
#else
    return false;
#endif
}

inline bool is_macos()
{
#ifdef IS_MACOS
    return true;
#else
    return false;
#endif
}

// Human-readable OS name.
inline const char* os_name()
{
#if defined(IS_WINDOWS)
    return "Windows";
#elif defined(IS_LINUX)
    return "Linux";
#elif defined(IS_MACOS)
    return "macOS";
#else
    return "Unknown";
#endif
}

}
}

#endif
