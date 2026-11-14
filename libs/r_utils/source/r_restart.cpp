
#include "r_utils/r_restart.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#ifdef IS_WINDOWS
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#endif
#ifdef IS_MACOS
#include <crt_externs.h>
#endif

using namespace r_utils;
using namespace std;

// How the new copy finds what to wait for: on Windows, the predecessor's pid; on
// Linux and macOS, the read end of a pipe whose write end only the predecessor
// holds, so it reads end-of-file once the predecessor has exited, however it
// exits, and whoever reaps it.
static const char* const WAIT_ENV = "R_RESTART_WAIT";

static vector<string> _without(vector<string> args, const vector<string>& drop_args)
{
    if(args.size() > 1)
    {
        args.erase(remove_if(args.begin() + 1, args.end(),
                             [&](const string& a) { return find(drop_args.begin(), drop_args.end(), a) != drop_args.end(); }),
                   args.end());
    }
    return args;
}

string r_restart::windows_command_line(const vector<string>& args)
{
    // The C runtime's rules: an arg with no space, tab or quote goes as it is;
    // any other is quoted, with backslashes doubled where they come before a
    // quote (an escaped one, or the closing one).
    string cmd;
    for(size_t i = 0; i < args.size(); ++i)
    {
        const string& a = args[i];
        if(i > 0)
            cmd += ' ';
        if(!a.empty() && a.find_first_of(" \t\n\v\"") == string::npos)
        {
            cmd += a;
            continue;
        }
        cmd += '"';
        size_t backslashes = 0;
        for(char c : a)
        {
            if(c == '\\')
            {
                ++backslashes;
                continue;
            }
            if(c == '"')
                cmd.append(backslashes * 2 + 1, '\\');
            else cmd.append(backslashes, '\\');
            backslashes = 0;
            cmd += c;
        }
        cmd.append(backslashes * 2, '\\');
        cmd += '"';
    }
    return cmd;
}

#ifdef IS_WINDOWS

static vector<string> _own_args()
{
    vector<string> args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if(!argv)
        return args;
    for(int i = 0; i < argc; ++i)
        args.push_back(r_string_utils::convert_wide_string_to_multi_byte_string(argv[i]));
    LocalFree(argv);
    return args;
}

bool r_restart::restart(bool, const vector<string>& drop_args, string& why)
{
    auto exe = r_fs::current_exe_path();
    auto args = _without(_own_args(), drop_args);
    if(exe.empty() || args.empty())
    {
        why = "couldn't find this program's path or arguments";
        return false;
    }
    args[0] = exe;

    auto wexe = r_string_utils::convert_multi_byte_string_to_wide_string(exe);
    auto wcmd = r_string_utils::convert_multi_byte_string_to_wide_string(windows_command_line(args));
    vector<wchar_t> cmd_buf(wcmd.begin(), wcmd.end());
    cmd_buf.push_back(L'\0');

    // The new copy inherits this process's environment, with the pid to wait for
    // added for it alone. No handles are inherited.
    auto wname = r_string_utils::convert_multi_byte_string_to_wide_string(WAIT_ENV);
    auto wpid = to_wstring(GetCurrentProcessId());
    SetEnvironmentVariableW(wname.c_str(), wpid.c_str());

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    BOOL ok = CreateProcessW(wexe.c_str(), cmd_buf.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    DWORD err = GetLastError();
    SetEnvironmentVariableW(wname.c_str(), NULL);

    if(!ok)
    {
        why = "couldn't start " + exe + " (error " + to_string((unsigned long)err) + ")";
        return false;
    }
    // Its window may come to the front once it's up.
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool r_restart::wait_for_predecessor(chrono::milliseconds timeout)
{
    auto wname = r_string_utils::convert_multi_byte_string_to_wide_string(WAIT_ENV);
    wchar_t buf[32] = {};
    DWORD n = GetEnvironmentVariableW(wname.c_str(), buf, 32);
    if(n == 0 || n >= 32)
        return false;
    // Not passed on to anything this copy starts.
    SetEnvironmentVariableW(wname.c_str(), NULL);
    _wputenv_s(wname.c_str(), L"");

    DWORD pid = (DWORD)wcstoul(buf, nullptr, 10);
    if(pid == 0)
        return false;
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if(h)
    {
        WaitForSingleObject(h, (DWORD)timeout.count());
        CloseHandle(h);
    }
    // No handle: it has already gone.
    return true;
}

#else

static vector<string> _own_args()
{
    vector<string> args;
#ifdef IS_MACOS
    int argc = *_NSGetArgc();
    char** argv = *_NSGetArgv();
    for(int i = 0; i < argc; ++i)
        args.push_back(argv[i]);
#else
    try
    {
        auto bytes = r_fs::read_file("/proc/self/cmdline");
        string cur;
        for(auto b : bytes)
        {
            if(b == 0)
            {
                args.push_back(cur);
                cur.clear();
            }
            else cur += (char)b;
        }
        if(!cur.empty())
            args.push_back(cur);
    }
    catch(...)
    {
    }
#endif
    return args;
}

static void _set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    if(flags >= 0)
        fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

bool r_restart::restart(bool headless, const vector<string>& drop_args, string& why)
{
#ifdef IS_LINUX
    // systemd's unit (Restart=always) starts it again.
    if(headless)
        return true;
#else
    (void)headless;
#endif

    string exe = r_fs::current_exe_path();
    auto args = _without(_own_args(), drop_args);
#ifdef IS_LINUX
    // The executable is inside the AppImage's mount, which goes when this
    // process does; the AppImage file itself is what to start.
    const char* appimage = getenv("APPIMAGE");
    if(appimage && appimage[0] != '\0')
        exe = appimage;
#endif
    if(exe.empty() || args.empty())
    {
        why = "couldn't find this program's path or arguments";
        return false;
    }
    args[0] = exe;

    // wait_pipe: the new copy waits on its read end, which only it keeps; its
    // write end stays open here (close-on-exec, so nothing else this process
    // starts holds it) until this process exits. exec_pipe: tells this process
    // whether the exec worked (end-of-file if it did, errno if not).
    int wait_pipe[2], exec_pipe[2];
    if(pipe(wait_pipe) != 0)
    {
        why = string("pipe failed: ") + strerror(errno);
        return false;
    }
    if(pipe(exec_pipe) != 0)
    {
        why = string("pipe failed: ") + strerror(errno);
        close(wait_pipe[0]);
        close(wait_pipe[1]);
        return false;
    }
    _set_cloexec(wait_pipe[1]);
    _set_cloexec(exec_pipe[0]);
    _set_cloexec(exec_pipe[1]);

    // Everything the child needs, built before fork: after it only
    // async-signal-safe calls are allowed.
    vector<char*> argv;
    for(auto& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    const string wait_var = string(WAIT_ENV) + "=" + to_string(wait_pipe[0]);
    const string prefix = string(WAIT_ENV) + "=";
    vector<char*> envp;
#ifdef IS_MACOS
    char** env = *_NSGetEnviron();
#else
    char** env = environ;
#endif
    for(char** e = env; e && *e; ++e)
    {
        if(strncmp(*e, prefix.c_str(), prefix.size()) != 0)
            envp.push_back(*e);
    }
    envp.push_back(const_cast<char*>(wait_var.c_str()));
    envp.push_back(nullptr);

    long max_fd = sysconf(_SC_OPEN_MAX);
    if(max_fd < 0 || max_fd > 65536)
        max_fd = 65536;

    pid_t pid = fork();
    if(pid < 0)
    {
        why = string("fork failed: ") + strerror(errno);
        close(wait_pipe[0]); close(wait_pipe[1]);
        close(exec_pipe[0]); close(exec_pipe[1]);
        return false;
    }
    if(pid == 0)
    {
        // Its own session, so a terminal's hangup doesn't reach it. Every
        // descriptor but the two pipe ends it needs is closed: an inherited
        // listening socket would keep the new copy's own bind from working.
        setsid();
        for(long fd = 3; fd < max_fd; ++fd)
        {
            if(fd != wait_pipe[0] && fd != exec_pipe[1])
                close((int)fd);
        }
        signal(SIGPIPE, SIG_DFL);
        execve(argv[0], argv.data(), envp.data());
        int e = errno;
        ssize_t ignored = write(exec_pipe[1], &e, sizeof(e));
        (void)ignored;
        _exit(127);
    }

    close(wait_pipe[0]);
    close(exec_pipe[1]);
    int child_errno = 0;
    ssize_t n;
    do
    {
        n = read(exec_pipe[0], &child_errno, sizeof(child_errno));
    } while(n < 0 && errno == EINTR);
    close(exec_pipe[0]);
    if(n > 0)
    {
        why = "couldn't start " + exe + ": " + strerror(child_errno);
        close(wait_pipe[1]);
        return false;
    }
    // wait_pipe[1] is left open on purpose: it closes when this process exits.
    return true;
}

bool r_restart::wait_for_predecessor(chrono::milliseconds timeout)
{
    const char* v = getenv(WAIT_ENV);
    if(!v || v[0] == '\0')
        return false;
    int fd = atoi(v);
    // Not passed on to anything this copy starts.
    unsetenv(WAIT_ENV);
    if(fd < 3)
        return false;
    _set_cloexec(fd);

    auto deadline = chrono::steady_clock::now() + timeout;
    while(true)
    {
        auto left = chrono::duration_cast<chrono::milliseconds>(deadline - chrono::steady_clock::now()).count();
        if(left <= 0)
            break;
        struct pollfd p;
        p.fd = fd;
        p.events = POLLIN;
        p.revents = 0;
        int r = poll(&p, 1, (int)min<long long>(left, 1000));
        if(r < 0 && errno == EINTR)
            continue;
        if(r < 0 || (p.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)))
            break;
    }
    close(fd);
    return true;
}

#endif
