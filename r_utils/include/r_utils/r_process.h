#ifndef _r_utils_r_process_h
#define _r_utils_r_process_h

#include "r_utils/r_string_utils.h"
#include "r_utils/r_file.h"

#ifdef IS_LINUX
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

#ifdef IS_LINUX
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
#ifdef IS_LINUX
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
#ifdef IS_LINUX
        return (pid == obj.pid)?true:false; 
#endif
    }
    bool valid() const
    {
#ifdef IS_WINDOWS
        return (pi.dwProcessId > 0)?true:false;
#endif
#ifdef IS_LINUX
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
#ifdef IS_LINUX
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
    r_process( const std::string& cmd );
    virtual ~r_process() noexcept;
    void start();
    bool running();
    r_pid get_pid() const { return _pid; }
    r_wait_status wait_for(int& code, std::chrono::milliseconds timeout);
    r_wait_status wait(int& code);
    void kill();

    static r_pid get_current_pid();

private:
    r_pid _pid;
    std::string _cmd;
};

}

#endif