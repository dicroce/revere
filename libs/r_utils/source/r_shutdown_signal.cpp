#include "r_utils/r_shutdown_signal.h"
#include "r_utils/r_logger.h"

#ifdef IS_WINDOWS
#include <windows.h>
#else
#include <csignal>
#include <pthread.h>
#endif

using namespace r_utils;
using namespace std;

#ifdef IS_WINDOWS
static HANDLE _stop_event = nullptr;

static BOOL WINAPI _console_ctrl_handler(DWORD ctrl_type)
{
    switch(ctrl_type)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if(_stop_event)
                SetEvent(_stop_event);
            return TRUE;
        default:
            return FALSE;
    }
}

void r_shutdown_signal::arm(const string& windows_event_name)
{
    AttachConsole(ATTACH_PARENT_PROCESS);
    _stop_event = CreateEventA(nullptr, TRUE, FALSE, ("Local\\" + windows_event_name).c_str());
    SetConsoleCtrlHandler(_console_ctrl_handler, TRUE);
}

void r_shutdown_signal::wait()
{
    if(_stop_event)
        WaitForSingleObject(_stop_event, INFINITE);
}
#else
static sigset_t _sigset;

void r_shutdown_signal::arm(const string&)
{
    // Threads started after this inherit the block, so only wait() receives them.
    sigemptyset(&_sigset);
    sigaddset(&_sigset, SIGINT);
    sigaddset(&_sigset, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &_sigset, nullptr);
}

void r_shutdown_signal::wait()
{
    int sig = 0;
    sigwait(&_sigset, &sig);
    R_LOG_INFO("Received signal %d.", sig);
}
#endif
