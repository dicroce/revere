#ifndef r_utils_mbedtls_threading_alt_h
#define r_utils_mbedtls_threading_alt_h

/*
 * mbedTLS MBEDTLS_THREADING_ALT mutex type for Windows.
 *
 * mbedTLS ships a pthread implementation but nothing for Win32, so on Windows
 * we supply our own mutex built on a CRITICAL_SECTION. The function pointers
 * are installed by r_utils at load time (see source/r_mbedtls_threading.cpp)
 * via mbedtls_threading_set_alt(), before any other mbedTLS call.
 *
 * Why threading is on at all: this build enables TLS 1.3 (and PSA crypto), and
 * the PSA key store is a process-wide singleton that is only thread-safe when
 * MBEDTLS_THREADING_C is defined. Without it, concurrent handshakes (several
 * ONVIF sessions, or the TLS web server's worker pool) corrupt each other.
 */

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

typedef struct mbedtls_threading_mutex_t
{
    CRITICAL_SECTION cs;
    char is_valid;
} mbedtls_threading_mutex_t;

#else
#error "threading_alt.h is only used on Windows; other platforms use MBEDTLS_THREADING_PTHREAD."
#endif

#endif
