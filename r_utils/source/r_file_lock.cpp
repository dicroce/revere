
#include "r_utils/r_file_lock.h"
#include "r_utils/r_exception.h"

#ifdef IS_WINDOWS
#include <Windows.h>
#include <io.h>
#else
#include <sys/file.h>
#endif

#include <utility>

using namespace r_utils;
using namespace std;

r_file_lock::r_file_lock(int fd) :
    _fd(fd)
{
}

r_file_lock::r_file_lock(r_file_lock&& obj) noexcept :
    _fd(std::move(obj._fd))
{
}

r_file_lock::~r_file_lock() noexcept
{
}

r_file_lock& r_file_lock::operator=(r_file_lock&& obj) noexcept
{
    _fd = std::move(obj._fd);
    obj._fd = -1;

    return *this;
}

#ifdef IS_WINDOWS

static BOOL _file_size (HANDLE h, DWORD * lower, DWORD * upper)
{
    *lower = GetFileSize (h, upper);
    return 1;
}

static BOOL _acquire_file_lock(HANDLE h, int non_blocking, int exclusive)
{
    DWORD size_lower, size_upper;
    if (!_file_size(h, &size_lower, &size_upper))
        return 0;

    OVERLAPPED ovlp;
    memset(&ovlp, 0, sizeof ovlp);

    int flags = 0;
    if (non_blocking)
        flags |= LOCKFILE_FAIL_IMMEDIATELY;
    if (exclusive)
        flags |= LOCKFILE_EXCLUSIVE_LOCK;

    return LockFileEx(h, flags, 0, size_lower, size_upper, &ovlp);
}

static BOOL _release_file_lock(HANDLE h)
{
    DWORD size_lower, size_upper;
    if(!_file_size(h, &size_lower, &size_upper))
        return 0;

    return UnlockFile(h, 0, 0, size_lower, size_upper);
}

#ifndef LOCK_SH
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_UN 8
#define LOCK_NB 4
#endif

int win_flock(int fd, int operation)
{
    HANDLE h = (HANDLE)_get_osfhandle(fd);

    if(h == INVALID_HANDLE_VALUE)
    {
        errno = EBADF;
        return -1;
    }

    int non_blocking = operation & LOCK_NB;
    operation &= ~LOCK_NB;

    DWORD res;
    switch(operation)
    {
    case LOCK_SH:
        res = _acquire_file_lock(h, non_blocking, 0);
    break;
    case LOCK_EX:
        res = _acquire_file_lock(h, non_blocking, 1);
    break;
    case LOCK_UN:
        res = _release_file_lock(h);
    break;
    default:
        errno = EINVAL;
        return -1;
    }

    if (!res)
    {
        DWORD err = GetLastError();
        switch(err)
        {
        case ERROR_LOCK_VIOLATION:
            errno = EAGAIN;
        break;

        case ERROR_NOT_ENOUGH_MEMORY:
            errno = ENOMEM;
        break;

        case ERROR_BAD_COMMAND:
            errno = EINVAL;
        break;

        default:
            errno = err;
        }

        return -1;
    }

    return 0;
}

#endif

void r_file_lock::lock(bool exclusive)
{
#ifdef IS_WINDOWS
    if(win_flock(_fd, (exclusive)?LOCK_EX:LOCK_SH) < 0)
        R_STHROW(r_internal_exception, ("Unable to flock() file."));
#else
    if(flock(_fd, (exclusive)?LOCK_EX:LOCK_SH) < 0)
        R_STHROW(r_internal_exception, ("Unable to flock() file."));
#endif
}

void r_file_lock::unlock()
{
#ifdef IS_WINDOWS
    if(win_flock(_fd, LOCK_UN) < 0)
        R_STHROW(r_internal_exception, ("Unable to un-flock()!"));
#else
    if(flock(_fd, LOCK_UN) < 0)
        R_STHROW(r_internal_exception, ("Unable to un-flock()!"));
#endif
}

r_file_lock_guard::r_file_lock_guard(r_file_lock& lok, bool exclusive) :
    _lok(lok)
{
    _lok.lock(exclusive);
}

r_file_lock_guard::~r_file_lock_guard() noexcept
{
    _lok.unlock();
}
