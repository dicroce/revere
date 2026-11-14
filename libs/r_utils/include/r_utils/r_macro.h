
#ifndef r_utils_r_macro_h_
#define r_utils_r_macro_h_

#ifdef IS_WINDOWS
#define R_API __declspec(dllexport)
#define _WINSOCKAPI_
#include <windows.h>
#endif

#if defined(IS_LINUX) || defined(IS_MACOS)
#define R_API
#endif

// Around a third-party header (GStreamer's) that the compiler warns about:
// MSVC's C4244 (narrowing), GCC's and Clang's deprecated declarations.
#if defined(_MSC_VER)
#define R_THIRD_PARTY_HEADERS_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4244))
#define R_THIRD_PARTY_HEADERS_END __pragma(warning(pop))
#elif defined(__GNUC__)
#define R_THIRD_PARTY_HEADERS_BEGIN _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define R_THIRD_PARTY_HEADERS_END _Pragma("GCC diagnostic pop")
#else
#define R_THIRD_PARTY_HEADERS_BEGIN
#define R_THIRD_PARTY_HEADERS_END
#endif

#define R_MACRO_BEGIN do {
#define R_MACRO_END }while(0)

#define R_MACRO_END_LOOP_FOREVER }while(1)

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

#ifdef IS_WINDOWS
#define FULL_MEM_BARRIER MemoryBarrier
#else
#if defined(IS_LINUX) || defined(IS_MACOS)
#define FULL_MEM_BARRIER __sync_synchronize
#endif
#endif

#endif
