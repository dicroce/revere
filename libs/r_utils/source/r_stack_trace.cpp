
#include "r_utils/r_stack_trace.h"
#include "r_utils/r_string_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>

#if defined(IS_LINUX) || defined(IS_MACOS)
#include <cxxabi.h>
#include <execinfo.h>
#include <dlfcn.h>
#endif
#ifdef IS_WINDOWS
#include <windows.h>
#include <mutex>
#endif

using namespace r_utils;
using namespace std;

#ifdef IS_WINDOWS

#pragma comment(lib, "dbghelp.lib")

// Some versions of imagehlp.dll lack the proper packing directives themselves
// so we need to do it.
#pragma pack( push, before_imagehlp, 8 )
#include <imagehlp.h>
#pragma pack( pop, before_imagehlp )

// DbgHelp is single-threaded: every Sym* call in the process must be
// serialized. This used to SymInitialize / SymFromAddr / SymCleanup on every
// call, unlocked, from whatever thread constructed an r_exception — and revere
// throws as control flow on many threads at once. Racing Init/Cleanup pairs
// double-free DbgHelp's internal module state (0xc0000374; caught in a dump
// 2026-10-13 inside SymInitialize under r_websocket::send_binary). So: one
// lock, one SymInitialize for the life of the process, never SymCleanup, and
// no symbol server (that made every throw a potential HTTP fetch).
static std::mutex& _dbghelp_lock()
{
    // Leaked on purpose: exceptions can be thrown during static destruction.
    static std::mutex* m = new std::mutex;
    return *m;
}

// Caller must hold _dbghelp_lock().
static void _dbghelp_init_once(HANDLE process)
{
    static bool initialized = false;
    if(initialized)
        return;
    initialized = true;

    DWORD opts = SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS;
    if(r_stack_trace::symbolize_enabled())
        opts |= SYMOPT_LOAD_LINES;
    ::SymSetOptions(opts);
    ::SymInitialize(process, NULL, TRUE);
}

static string generate_stack(char sep)
{
    PVOID addrs[64] = { 0 };
    // Skip generate_stack itself.
    USHORT frames = CaptureStackBackTrace(1, 64, addrs, NULL);

    const bool symbolize = r_stack_trace::symbolize_enabled();
    HANDLE process = ::GetCurrentProcess();
    std::ostringstream out;

    std::lock_guard<std::mutex> g(_dbghelp_lock());
    _dbghelp_init_once(process);

    bool refreshed = false;
    for(USHORT i = 0; i < frames; ++i)
    {
        DWORD64 addr = (DWORD64)addrs[i];

        // Image + load base come straight from the loader, not DbgHelp.
        HMODULE mod = NULL;
        char image[MAX_PATH] = "?";
        if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addrs[i], &mod))
            GetModuleFileNameA(mod, image, sizeof(image));
        const char* image_base = strrchr(image, '\\');
        image_base = image_base ? image_base + 1 : image;
        DWORD64 base = (DWORD64)mod;

        // Plugins load after SymInitialize; pick them up the first time a
        // frame lands in a module DbgHelp doesn't know about.
        if(!refreshed && mod && ::SymGetModuleBase64(process, addr) == 0)
        {
            ::SymRefreshModuleList(process);
            refreshed = true;
        }

        ULONG64 buffer[(sizeof(SYMBOL_INFO) + 1024 + sizeof(ULONG64) - 1) / sizeof(ULONG64)] = { 0 };
        SYMBOL_INFO* info = (SYMBOL_INFO*)buffer;
        info->SizeOfStruct = sizeof(SYMBOL_INFO);
        info->MaxNameLen = 1024;
        DWORD64 displacement = 0;
        string func = (::SymFromAddr(process, addr, &displacement, info)) ? string(info->Name, info->NameLen) : string("???");

        string location;
        if(symbolize)
        {
            IMAGEHLP_LINE64 line = { 0 };
            line.SizeOfStruct = sizeof(line);
            DWORD line_disp = 0;
            if(::SymGetLineFromAddr64(process, addr, &line_disp, &line))
                location = string(line.FileName) + "(" + to_string(line.LineNumber) + ")";
        }

        if(location.empty())
        {
            char buf[512];
            snprintf(buf, sizeof(buf), "(in %s) +0x%llx [base 0x%llx addr 0x%llx]",
                     image_base, (unsigned long long)(addr - base), (unsigned long long)base, (unsigned long long)addr);
            location = buf;
        }

        out << "[#" << (frames - i) << "] " << func << " @ " << location << sep;
    }

    return out.str();
}

#endif

// Should frames be resolved to file:line with an external symbolizer
// (atos on macOS, addr2line on Linux)? Default OFF: that path spawns one
// subprocess per frame (~30 per exception) and costs seconds per throw,
// and r_exception captures a trace on every construction — including the
// routine 404 / file-not-found / timeout throws the server uses as control
// flow. Function names still come from dladdr() in-process; the raw
// address + image load base is printed so file:line can be recovered
// offline:   atos -o <image> -l <base> <addr>   /   addr2line -e <image> <offset>
// Opt back in for local debugging with REVERE_SYMBOLIZE_TRACES=1.
bool r_utils::r_stack_trace::symbolize_enabled()
{
    static const bool enabled = [](){
        const char* v = getenv("REVERE_SYMBOLIZE_TRACES");
        if(!v || !*v) return false;
        // Never shell out in sandboxed environments (Flatpak/Snap): binutils isn't available.
        if(getenv("FLATPAK_ID") != nullptr || getenv("SNAP") != nullptr) return false;
        return !(v[0] == '0' && v[1] == '\0');
    }();
    return enabled;
}

string r_utils::r_stack_trace::get_stack(char sep)
{
#ifdef IS_WINDOWS
    return generate_stack(sep);
#else
    void* trace[256];
    int trace_size = backtrace(trace, 256);
    std::ostringstream out;

    const bool symbolize = symbolize_enabled();

    for (int i = 1; i < trace_size; ++i)
    {
        Dl_info info;
        if (!dladdr(trace[i], &info))
            continue;

        std::string func;
        if (info.dli_sname)
        {
            int status = 0;
            char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            if (status == 0 && demangled)
            {
                func = demangled;
                free(demangled);
            } else func = info.dli_sname;
        } else func = "???";

        // PIE-aware address resolution
        uintptr_t addr = reinterpret_cast<uintptr_t>(trace[i]);
        uintptr_t base = reinterpret_cast<uintptr_t>(info.dli_fbase);
        uintptr_t offset = addr - base;

        const char* image = info.dli_fname ? info.dli_fname : "?";
        const char* image_base = strrchr(image, '/');
        image_base = image_base ? image_base + 1 : image;

        std::string location;

        if (symbolize)
        {
            char cmd[512];
#ifdef IS_MACOS
            snprintf(cmd, sizeof(cmd), "atos -o %s -l 0x%lx 0x%lx", image, (unsigned long)base, (unsigned long)addr);
#else
            snprintf(cmd, sizeof(cmd), "addr2line -f -p -e %s 0x%lx", image, (unsigned long)offset);
#endif
            FILE* fp = popen(cmd, "r");
            if (fp)
            {
                char buf[512];
                if (fgets(buf, sizeof(buf), fp))
                {
                    location = buf;
                    if (!location.empty() && location.back() == '\n')
                        location.pop_back();
                }
                pclose(fp);
            }
        }

        if (location.empty())
        {
            // In-process fallback: image + offset is enough to symbolize offline.
            char buf[256];
            snprintf(buf, sizeof(buf), "(in %s) +0x%lx [base 0x%lx addr 0x%lx]",
                     image_base, (unsigned long)offset, (unsigned long)base, (unsigned long)addr);
            location = buf;
        }

        out << "[#" << (trace_size - i) << "] " << func << " @ " << location << sep;
    }

    return out.str();
#endif
}
