
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
#include <string>
#include <sstream>
#include <vector>
#include <Psapi.h>
#include <algorithm>
#include <iterator>
#endif

using namespace r_utils;
using namespace std;

#ifdef IS_WINDOWS

static char stack_buffer[32768];

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dbghelp.lib")

// Some versions of imagehlp.dll lack the proper packing directives themselves
// so we need to do it.
#pragma pack( push, before_imagehlp, 8 )
#include <imagehlp.h>
#pragma pack( pop, before_imagehlp )

struct module_data {
    std::string image_name;
    std::string module_name;
    void *base_address;
    DWORD load_size;
};

DWORD generate_stack_trace(char* buffer, EXCEPTION_POINTERS *ep );

class symbol 
{
    typedef IMAGEHLP_SYMBOL64 sym_type;
    sym_type *sym;
    static const int max_name_len = 1024;

public:
    symbol(HANDLE process, DWORD64 address) : 
        sym((sym_type *)::operator new(sizeof(*sym) + max_name_len))
    {
        memset(sym, '\0', sizeof(*sym) + max_name_len);
        sym->SizeOfStruct = sizeof(*sym);
        sym->MaxNameLength = max_name_len;
        DWORD64 displacement;

        SymGetSymFromAddr64(process, address, &displacement, sym);
    }

    std::string name() { return std::string(sym->Name); }
    std::string undecorated_name()
    { 
        if (*sym->Name == '\0')
            return "<couldn't map PC to fn name>";
        std::vector<char> und_name(max_name_len);
        UnDecorateSymbolName(sym->Name, &und_name[0], max_name_len, UNDNAME_COMPLETE);
        return std::string(&und_name[0], strlen(&und_name[0]));
    }
};

class get_mod_info
{
    HANDLE process;
    static const int buffer_length = 4096;
public:
    get_mod_info(HANDLE h) : process(h) {}

    module_data operator()(HMODULE module)
    { 
        module_data ret;
        char temp[buffer_length];
        MODULEINFO mi;

        GetModuleInformation(process, module, &mi, sizeof(mi));
        ret.base_address = mi.lpBaseOfDll;
        ret.load_size = mi.SizeOfImage;

        GetModuleFileNameEx(process, module, temp, sizeof(temp));
        ret.image_name = temp;
        GetModuleBaseName(process, module, temp, sizeof(temp));
        ret.module_name = temp;
        std::vector<char> img(ret.image_name.begin(), ret.image_name.end());
        std::vector<char> mod(ret.module_name.begin(), ret.module_name.end());
        SymLoadModule64(process, 0, &img[0], &mod[0], (DWORD64)ret.base_address, ret.load_size);
        return ret;
    }
};

#if 0

// if you use C++ exception handling: install a translator function
// with set_se_translator(). In the context of that function (but *not*
// afterwards), you can either do your stack dump, or save the CONTEXT
// record as a local copy. Note that you must do the stack dump at the
// earliest opportunity, to avoid the interesting stack-frames being gone
// by the time you do the dump.
DWORD generate_stack_trace(char* buffer, EXCEPTION_POINTERS *ep) 
{
    HANDLE process = GetCurrentProcess();
    HANDLE hThread = GetCurrentThread();
    int frame_number=0;
    DWORD offset_from_symbol=0;
    IMAGEHLP_LINE64 line = {0};
    std::vector<module_data> modules;
    DWORD cbNeeded;
    std::vector<HMODULE> module_handles(1);

    // Load the symbols:
    // WARNING: You'll need to replace <pdb-search-path> with either NULL
    // or some folder where your clients will be able to find the .pdb file.
    if (!SymInitialize(process, NULL, false)) 
        throw(std::logic_error("Unable to initialize symbol handler"));
    DWORD symOptions = SymGetOptions();
    symOptions |= SYMOPT_LOAD_LINES | SYMOPT_UNDNAME;
    SymSetOptions(symOptions);
    EnumProcessModules(process, &module_handles[0], ((DWORD)module_handles.size() * sizeof(HMODULE)), (LPDWORD)&cbNeeded);
    module_handles.resize(cbNeeded/sizeof(HMODULE));
    EnumProcessModules(process, &module_handles[0], ((DWORD)module_handles.size() * sizeof(HMODULE)), (LPDWORD)&cbNeeded);
    std::transform(module_handles.begin(), module_handles.end(), std::back_inserter(modules), get_mod_info(process));
    void *base = modules[0].base_address;

    // Setup stuff:
    CONTEXT* context = ep->ContextRecord;
#ifdef _M_X64
    STACKFRAME64 frame;
    frame.AddrPC.Offset = context->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;    
    frame.AddrFrame.Offset = context->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
#else
    STACKFRAME64 frame;
    frame.AddrPC.Offset = context->Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Esp;
    frame.AddrStack.Mode = AddrModeFlat;    
    frame.AddrFrame.Offset = context->Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
#endif
    line.SizeOfStruct = sizeof line;
    IMAGE_NT_HEADERS *h = ImageNtHeader(base);
    DWORD image_type = h->FileHeader.Machine;
    int n = 0;

    // Build the string:
    std::ostringstream builder;
    do {
        if ( frame.AddrPC.Offset != 0 ) {
            std::string fnName = symbol(process, frame.AddrPC.Offset).undecorated_name();
            builder << "[" << n << "] ";
            builder << fnName;
            if (SymGetLineFromAddr64( process, frame.AddrPC.Offset, &offset_from_symbol, &line)) 
                builder << "  " << line.FileName << "(" << line.LineNumber << ")\n";
            else builder << "\n";
            if (fnName == "main")
                break;
            if (fnName == "RaiseException") {
                return EXCEPTION_EXECUTE_HANDLER;
            }
        }
        else
            builder << "(No Symbols: PC == 0)";
        if (!StackWalk64(image_type, process, hThread, &frame, context, NULL, 
                            SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;
        if (++n > 20)
            break;
    } while (frame.AddrReturn.Offset != 0);
    //return EXCEPTION_EXECUTE_HANDLER;
    SymCleanup(process);

    auto s = builder.str();
    auto s_size = (s.length() < 32767)?s.length():32767;
    buffer[s_size] = 0;
    memcpy(buffer, s.c_str(), s_size);

    return EXCEPTION_EXECUTE_HANDLER;
}

static void _stack_bootstrap()
{
    __try {
        RaiseException(EXCEPTION_BREAKPOINT, 0, 0, NULL);
    }
    __except(generate_stack_trace(stack_buffer, GetExceptionInformation())) {
    }
}
#endif

static string generate_stack()
{
    string out;
    // Set up the symbol options so that we can gather information from the current
    // executable's PDB files, as well as the Microsoft symbol servers.  We also want
    // to undecorate the symbol names we're returned.  If you want, you can add other
    // symbol servers or paths via a semi-colon separated list in SymInitialized.
    ::SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_INCLUDE_32BIT_MODULES | SYMOPT_UNDNAME);
    if(!::SymInitialize(::GetCurrentProcess(), "http://msdl.microsoft.com/download/symbols", TRUE))
        return string();
 
    // Capture up to 25 stack frames from the current call stack.  We're going to
    // skip the first stack frame returned because that's the GetStackWalk function
    // itself, which we don't care about.
    PVOID addrs[ 25 ] = { 0 };
    USHORT frames = CaptureStackBackTrace( 1, 25, addrs, NULL );
 
    for (USHORT i = 0; i < frames; i++) {
        // Allocate a buffer large enough to hold the symbol information on the stack and get 
        // a pointer to the buffer.  We also have to set the size of the symbol structure itself
        // and the number of bytes reserved for the name.
        ULONG64 buffer[ (sizeof( SYMBOL_INFO ) + 1024 + sizeof( ULONG64 ) - 1) / sizeof( ULONG64 ) ] = { 0 };
        SYMBOL_INFO *info = (SYMBOL_INFO *)buffer;
        info->SizeOfStruct = sizeof( SYMBOL_INFO );
        info->MaxNameLen = 1024;
 
        // Attempt to get information about the symbol and add it to our output parameter.
        DWORD64 displacement = 0;
        if (::SymFromAddr( ::GetCurrentProcess(), (DWORD64)addrs[ i ], &displacement, info )) {
            out.append( "[" + std::to_string( i ) + "] ");
            out.append( info->Name, info->NameLen );
            out.append( "\n" );
        }
    }
 
    ::SymCleanup(::GetCurrentProcess());

    return out; 
}

#endif

string r_utils::r_stack_trace::get_stack(char sep)
{
#ifdef IS_WINDOWS
    return generate_stack();
#else
    void* trace[256];
    int trace_size = backtrace(trace, 256);
    std::ostringstream out;

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

        char cmd[512];
#ifdef IS_MACOS
        snprintf(cmd, sizeof(cmd), "atos -o %s -l 0x%lx 0x%lx", info.dli_fname, base, addr);
#else
        snprintf(cmd, sizeof(cmd), "addr2line -f -p -e %s 0x%lx", info.dli_fname, offset);
#endif
        FILE* fp = popen(cmd, "r");

        std::string location = "?? ??:0";
        if (fp)
        {
            char buf[512];
            if (fgets(buf, sizeof(buf), fp))
            {
                location = buf;
                // Remove trailing newline
                if (!location.empty() && location.back() == '\n')
                    location.pop_back();
            }
            pclose(fp);
        }

        out << "[#" << (trace_size - i) << "] " << func << " @ " << location << sep;
    }

    return out.str();
#endif
}
