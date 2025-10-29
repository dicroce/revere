
#ifndef r_utils_r_file_lock_h
#define r_utils_r_file_lock_h

#include "r_utils/r_macro.h"

class test_r_utils_r_file_lock;

namespace r_utils
{

class r_file_lock final
{
public:
    R_API r_file_lock(int fd=-1);
    R_API r_file_lock(const r_file_lock&) = delete;
    R_API r_file_lock( r_file_lock&& obj ) noexcept;
    R_API ~r_file_lock() noexcept;

    R_API r_file_lock& operator=(const r_file_lock&) = delete;
    R_API r_file_lock& operator=(r_file_lock&& obj) noexcept;

    R_API void lock(bool exclusive = true);
    R_API void unlock();

private:
    int _fd;
};

class r_file_lock_guard final
{
public:
    R_API r_file_lock_guard(r_file_lock& lok, bool exclusive = true);
    R_API r_file_lock_guard(const r_file_lock_guard&) = delete;
    R_API r_file_lock_guard(r_file_lock&&) = delete;
    R_API ~r_file_lock_guard() noexcept;

    R_API r_file_lock_guard& operator=(const r_file_lock_guard&) = delete;
    R_API r_file_lock_guard& operator=(r_file_lock_guard&&) = delete;

private:
    r_file_lock& _lok;
};

}

#endif
