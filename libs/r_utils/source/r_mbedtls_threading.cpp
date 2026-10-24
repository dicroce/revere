// Installs the Windows mutex implementation mbedTLS uses when built with
// MBEDTLS_THREADING_ALT (see include/mbedtls/threading_alt.h). It runs as a
// static initializer when r_utils loads, which satisfies mbedTLS's requirement
// that mbedtls_threading_set_alt() be called before any other mbedTLS function.
// On POSIX mbedTLS uses its own pthread implementation and this file is empty.

extern "C" {
#include <mbedtls/threading.h>
}

#if defined(MBEDTLS_THREADING_ALT)

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
    // process may still be using mbedTLS while r_utils unloads, and leaking a
    // few critical sections at exit is harmless.
};

const _installer _install_mbedtls_threading;

}

#endif
