
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

// Lower-case OS id for file names and manifests: "windows", "macos" or "linux".
inline const char* os_id()
{
#if defined(IS_WINDOWS)
    return "windows";
#elif defined(IS_MACOS)
    return "macos";
#else
    return "linux";
#endif
}

// What a program's file name ends with: ".exe" on Windows, nothing elsewhere.
inline const char* exe_suffix()
{
#ifdef IS_WINDOWS
    return ".exe";
#else
    return "";
#endif
}

// CPU architecture this was built for: "x86_64", "arm64" or "unknown".
inline const char* arch_name()
{
#if defined(_M_X64) || defined(__x86_64__)
    return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#else
    return "unknown";
#endif
}

}
}

#endif
