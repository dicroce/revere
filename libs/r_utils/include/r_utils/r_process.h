#ifndef _r_utils_r_process_h
#define _r_utils_r_process_h

#include "r_utils/r_string_utils.h"
#include "r_utils/r_file.h"

#if defined(IS_LINUX) || defined(IS_MACOS)
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#endif
#ifdef IS_WINDOWS
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#endif
#include <list>
#include <chrono>

namespace r_utils
{

struct r_pid
{

#if defined(IS_LINUX) || defined(IS_MACOS)
    pid_t pid;
#endif
#ifdef IS_WINDOWS
    PROCESS_INFORMATION pi;
#endif
    r_pid()
    {
#ifdef IS_WINDOWS
        pi.hProcess = NULL;
        pi.hThread = NULL;
        pi.dwProcessId = 0;
        pi.dwThreadId = 0;
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
        pid = -1;
#endif
    }
    r_pid(const r_pid& obj) = default;
    r_pid(r_pid&&) = default;
    r_pid& operator=(const r_pid&) = default;
    r_pid& operator=(r_pid&&) = default;
    bool operator==(const r_pid& obj)
    {
#ifdef IS_WINDOWS
        return (pi.dwProcessId == obj.pi.dwProcessId)?true:false;
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
        return (pid == obj.pid)?true:false;
#endif
    }
    bool valid() const
    {
#ifdef IS_WINDOWS
        return (pi.dwProcessId > 0)?true:false;
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
        return (pid >= 0)?true:false;
#endif
    }
    void clear()
    {
#ifdef IS_WINDOWS
        pi.hProcess = NULL;
        pi.hThread = NULL;
        pi.dwProcessId = 0;
        pi.dwThreadId = 0;
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
        pid = -1;
#endif
    }
};

enum r_wait_status
{
    R_PROCESS_EXITED,
    R_PROCESS_WAIT_TIMEDOUT
};

class r_process
{
public:
    R_API r_process( const std::string& cmd, bool detached = false );
    R_API virtual ~r_process() noexcept;
    R_API void start();
    R_API bool running();
    R_API r_pid get_pid() const { return _pid; }
    R_API r_wait_status wait_for(int& code, std::chrono::milliseconds timeout);
    R_API r_wait_status wait(int& code);
    R_API void kill();

    R_API static r_pid get_current_pid();
    R_API bool is_detached() const { return _detached; }

private:
    r_pid _pid;
    std::string _cmd;
    bool _detached;
};

}

#endif