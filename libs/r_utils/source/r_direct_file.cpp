#include "r_utils/r_direct_file.h"
#include "r_utils/r_exception.h"
#include <cstdlib>

#ifdef IS_WINDOWS
#include <windows.h>
#include <malloc.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace r_utils;
using namespace std;

static void _aligned_free_fn(void* p)
{
#ifdef IS_WINDOWS
    _aligned_free(p);
#else
    free(p);
#endif
}

r_direct_file::buffer r_direct_file::aligned_buffer(size_t size)
{
    void* p = nullptr;
#ifdef IS_WINDOWS
    p = _aligned_malloc(size, ALIGNMENT);
#else
    if(posix_memalign(&p, ALIGNMENT, size) != 0)
        p = nullptr;
#endif
    if(!p)
        R_THROW(("Unable to allocate %llu aligned bytes.", (unsigned long long)size));
    return buffer((uint8_t*)p, _aligned_free_fn);
}

r_direct_file::r_direct_file(const string& path)
{
#ifdef IS_WINDOWS
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if(h == INVALID_HANDLE_VALUE)
        R_THROW(("Unable to create %s (error %lu).", path.c_str(), GetLastError()));
    _h = h;
#else
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef IS_LINUX
    flags |= O_DIRECT;
#endif
    _fd = open(path.c_str(), flags, 0644);
    if(_fd < 0)
        R_THROW(("Unable to create %s.", path.c_str()));
#endif
}

r_direct_file::~r_direct_file() noexcept
{
    close();
}

void r_direct_file::write(const uint8_t* data, size_t len)
{
    size_t done = 0;
    while(done < len)
    {
#ifdef IS_WINDOWS
        DWORD written = 0;
        if(!WriteFile((HANDLE)_h, data + done, (DWORD)(len - done), &written, NULL))
            R_THROW(("Write failed (error %lu).", GetLastError()));
        done += written;
#else
        ssize_t written = ::write(_fd, data + done, len - done);
        if(written < 0)
            R_THROW(("Write failed."));
        done += (size_t)written;
#endif
    }
}

void r_direct_file::sync()
{
#ifdef IS_WINDOWS
    // Written through already.
#else
    fsync(_fd);
#endif
}

void r_direct_file::close() noexcept
{
#ifdef IS_WINDOWS
    if(_h)
    {
        CloseHandle((HANDLE)_h);
        _h = nullptr;
    }
#else
    if(_fd >= 0)
    {
        ::close(_fd);
        _fd = -1;
    }
#endif
}
