
#include "r_utils/r_crash_handler.h"
#include "r_utils/r_logger.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>

#ifdef IS_WINDOWS
#include <windows.h>
#include <dbghelp.h>
#include <cstdlib>
#pragma comment(lib, "dbghelp.lib")
#endif

using namespace std;

namespace
{
    string _dump_dir;
    string _name_prefix;

    // A crash inside the crash handler would loop forever (or deadlock on the
    // loader lock). One dump per process is all we need.
    std::atomic<bool> _dump_written {false};
}

#ifdef IS_WINDOWS

namespace
{
    // MiniDumpWithFullMemory is the point of the exercise: heap corruption can
    // only be analysed with the heap present, and !heap -p -a needs the full
    // address space. The extra streams make thread and handle state readable
    // without guessing.
    constexpr MINIDUMP_TYPE DUMP_TYPE = (MINIDUMP_TYPE)(
        MiniDumpWithFullMemory |
        MiniDumpWithHandleData |
        MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules |
        MiniDumpWithProcessThreadData
    );

    string _dump_path()
    {
        SYSTEMTIME st;
        GetSystemTime(&st);

        char buf[512];
        snprintf(buf, sizeof(buf), "%s%s%s%lu_%04u%02u%02u-%02u%02u%02u.dmp",
                 _dump_dir.c_str(),
                 (!_dump_dir.empty() && _dump_dir.back() != '\\' && _dump_dir.back() != '/') ? "\\" : "",
                 _name_prefix.c_str(),
                 (unsigned long)GetCurrentProcessId(),
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return string(buf);
    }

    // ep may be null (CRT invalid-parameter, purecall, abort, explicit call).
    // MiniDumpWriteDump accepts a null ExceptionParam — we lose the "faulting
    // thread" marker but still capture every thread's stack, which is what
    // matters for those paths.
    bool _write_dump(EXCEPTION_POINTERS* ep, const char* reason)
    {
        if(_dump_dir.empty())
            return false;

        bool expected = false;
        if(!_dump_written.compare_exchange_strong(expected, true))
            return false;  // already dumped; a second fault is noise

        auto path = _dump_path();

        HANDLE f = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if(f == INVALID_HANDLE_VALUE)
            return false;

        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;

        // NOTE: MiniDumpWriteDump allocates, so writing a dump from a process
        // whose heap is already corrupt can itself fail. That is exactly the
        // case we care about most, which is why WER LocalDumps (written by an
        // external process) remains the more reliable backstop — this handler
        // complements it rather than replacing it.
        BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                    f, DUMP_TYPE, ep ? &mei : nullptr,
                                    nullptr, nullptr);
        CloseHandle(f);

        if(!ok)
        {
            DeleteFileA(path.c_str());
            // Deliberately not R_LOG_* here — allocation may be unsafe. The
            // caller's log line (written before the fault) is enough.
            fprintf(stderr, "revere: MiniDumpWriteDump failed (0x%08lx) for %s\n",
                    (unsigned long)GetLastError(), reason ? reason : "crash");
            fflush(stderr);
            return false;
        }

        fprintf(stderr, "revere: crash (%s) -- dump written to %s\n",
                reason ? reason : "unknown", path.c_str());
        fflush(stderr);
        return true;
    }

    LONG WINAPI _unhandled_exception_filter(EXCEPTION_POINTERS* ep)
    {
        _write_dump(ep, "unhandled exception");

        // CONTINUE_SEARCH, not EXECUTE_HANDLER: let the default handler run so
        // WER still gets its shot. Swallowing it here would disable the very
        // LocalDumps mechanism we rely on when the in-process dump fails.
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Names deliberately not _invalid_parameter_handler / _purecall_handler:
    // those are CRT typedef names in <stdlib.h> and collide.
    void _on_invalid_parameter(const wchar_t*, const wchar_t*,
                               const wchar_t*, unsigned int, uintptr_t)
    {
        // The CRT's default for this is to kill the process silently — no
        // exception, no log line. Catch it so it's at least attributable.
        _write_dump(nullptr, "CRT invalid parameter");
        abort();
    }

    void _on_purecall()
    {
        _write_dump(nullptr, "pure virtual call");
        abort();
    }

    void _on_abort(int)
    {
        _write_dump(nullptr, "abort");
    }
}

void r_utils::r_crash_handler::install_crash_handler(const std::string& dump_dir,
                                                     const std::string& name_prefix)
{
    _dump_dir = dump_dir;
    _name_prefix = name_prefix;

    SetUnhandledExceptionFilter(_unhandled_exception_filter);
    _set_invalid_parameter_handler(_on_invalid_parameter);
    _set_purecall_handler(_on_purecall);
    signal(SIGABRT, _on_abort);

    // Deliberately NOT calling _set_abort_behavior() to clear _CALL_REPORTFAULT.
    // That would stop abort() from invoking Windows Error Reporting, which is
    // what produces the WER records and any configured LocalDumps entry — the
    // 0xc0000409 aborts in this app's crash history were only visible because
    // of it. Our SIGABRT handler returns, so abort() proceeds to its normal
    // termination and WER still runs afterwards. Losing that redundancy to
    // suppress a message is a bad trade.

    R_LOG_INFO("Crash handler installed; dumps -> %s", _dump_dir.c_str());
}

void r_utils::r_crash_handler::write_dump_now(const char* reason)
{
    _write_dump(nullptr, reason);
}

#else

// Linux/macOS: the OS core-dump path is strictly better than anything we could
// do from inside a dying process, so this stays a no-op rather than pretending
// to add value. If a core isn't being produced, that's a ulimit/coredump-
// pattern configuration issue, not something to solve here.
void r_utils::r_crash_handler::install_crash_handler(const std::string&,
                                                     const std::string&)
{
}

void r_utils::r_crash_handler::write_dump_now(const char*)
{
}

#endif

std::string r_utils::r_crash_handler::get_dump_dir()
{
    return _dump_dir;
}
