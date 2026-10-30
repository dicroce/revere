// Installs the Windows mutex implementation mbedTLS uses when built with
// MBEDTLS_THREADING_ALT (see include/mbedtls/threading_alt.h).
//
// mbedTLS is a static library, so every DLL that links its code (r_utils,
// r_vss, the cloud plugin's libdatachannel, ...) carries a private copy with
// its own mutex function pointers, which start out as stubs that fail every
// lock. This file is compiled INTO the mbedcrypto library, and threading_alt.h
// (included by every mbedTLS source that uses threading) makes the linker pull
// it in wherever such code is linked. So each copy runs this static
// initializer when its DLL loads, before anything in that DLL can call into
// mbedTLS: mbedTLS requires mbedtls_threading_set_alt() before any other call.

extern "C" {
#include <mbedtls/threading.h>
}

#if defined(MBEDTLS_THREADING_ALT)

// Referenced by the /include linker directive in threading_alt.h.
extern "C" int r_mbedtls_threading_anchor = 1;

namespace
{

void _mutex_init(mbedtls_threading_mutex_t* m)
{
    if(!m)
        return;
    InitializeCriticalSection(&m->cs);
    m->is_valid = 1;
}

void _mutex_free(mbedtls_threading_mutex_t* m)
{
    if(!m || !m->is_valid)
        return;
    DeleteCriticalSection(&m->cs);
    m->is_valid = 0;
}

int _mutex_lock(mbedtls_threading_mutex_t* m)
{
    if(!m || !m->is_valid)
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    EnterCriticalSection(&m->cs);
    return 0;
}

int _mutex_unlock(mbedtls_threading_mutex_t* m)
{
    if(!m || !m->is_valid)
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    LeaveCriticalSection(&m->cs);
    return 0;
}

struct _installer
{
    _installer()
    {
        mbedtls_threading_set_alt(_mutex_init, _mutex_free, _mutex_lock, _mutex_unlock);
    }
    // No mbedtls_threading_free_alt() on teardown: other static objects in the
    // module may still be using mbedTLS while it unloads, and leaking a few
    // critical sections at exit is harmless.
};

const _installer _install_mbedtls_threading;

}

#endif
