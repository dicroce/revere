
#ifndef r_utils_r_crash_handler_h
#define r_utils_r_crash_handler_h

#include "r_utils/r_macro.h"
#include <string>

namespace r_utils
{

// Writes a crash dump when the process dies in a way ordinary error handling
// can't report.
//
// Why this exists: a run of heap-corruption crashes (STATUS_HEAP_CORRUPTION,
// 0xc0000374) killed revere every few days with nothing whatsoever in the log.
// Three things conspired: the OS kills the process without unwinding, so the
// std::terminate handler never runs; the log was buffered and lost its last
// seconds; and no minidump was retained. Reconstructing any of it depended on
// Windows Error Reporting metadata that only says "somewhere in ntdll".
//
// Installing this means a crashing install leaves a dump behind on its own,
// with no per-machine registry setup.
namespace r_crash_handler
{

// Install the process-wide handlers. dump_dir must already exist; dumps are
// written as <name_prefix><pid>_<UTC timestamp>.dmp. Safe to call once, early
// in main() — before any worker threads start, so nothing can crash unwatched.
//
// Windows: hooks unhandled SEH exceptions, CRT invalid-parameter and purecall,
// and SIGABRT. The handler deliberately does NOT swallow the exception — it
// returns EXCEPTION_CONTINUE_SEARCH so Windows Error Reporting still runs
// afterwards and any configured WER LocalDumps entry still fires. Two dumps
// beat none.
//
// Linux/macOS: no-op. The OS already provides this via core dumps
// (ulimit -c / systemd-coredump), which don't depend on the dying process
// being healthy enough to write a file.
R_API void install_crash_handler(const std::string& dump_dir, const std::string& name_prefix);

// Write a dump for the calling thread right now, outside of any fault. Used by
// the std::terminate handler so an unhandled C++ exception is as debuggable as
// a hard fault. No-op if install_crash_handler() was never called.
// reason is recorded in the log line that accompanies the dump.
R_API void write_dump_now(const char* reason);

// Directory dumps are written to, or "" if not installed.
R_API std::string get_dump_dir();

}

}

#endif
