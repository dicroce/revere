#include "r_utils/r_desktop.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"

#ifdef IS_WINDOWS
#include <windows.h>
#include <shellapi.h>
#else
#include <errno.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
extern char** environ;
#endif

using namespace r_utils;
using namespace std;

#ifndef IS_WINDOWS
// Starts opener with target as its one argument, and a thread that reaps it
// and logs a failure. -1 if it couldn't be started.
static int _start_opener(const char* opener, const string& target)
{
    char* argv[] = {const_cast<char*>(opener), const_cast<char*>(target.c_str()), nullptr};
    pid_t pid;
    int err = posix_spawnp(&pid, opener, nullptr, nullptr, argv, environ);
    if(err != 0)
        return -1;
    thread([pid, name = string(opener)]() {
        int status = 0;
        while(waitpid(pid, &status, 0) < 0)
        {
            if(errno != EINTR)
                return;
        }
        if(!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            R_LOG_ERROR("%s exited with %d", name.c_str(), WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    }).detach();
    return 0;
}
#endif

void r_desktop::open(const string& target)
{
#ifdef IS_WINDOWS
    HINSTANCE result = ShellExecuteA(NULL, "open", target.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if((INT_PTR)result <= 32)
        R_THROW(("ShellExecute couldn't open %s (error code %lld: %s)", target.c_str(), (long long)(INT_PTR)result, last_error_to_string().c_str()));
#else
#ifdef IS_MACOS
    const char* opener = "open";
#else
    const char* opener = "xdg-open";
#endif
    if(_start_opener(opener, target) != 0)
        R_THROW(("Could not run %s to open %s", opener, target.c_str()));
#endif
}
