#include "r_utils/r_dynamic_library.h"
#include "r_utils/r_exception.h"

#ifdef IS_WINDOWS
#else
#include <dlfcn.h>
#endif

using namespace r_utils;
using namespace std;

r_dynamic_library::r_dynamic_library() :
    _library_handle(nullptr),
    _library_name()
{
}

r_dynamic_library::r_dynamic_library(const string& library_name) :
    _library_handle(nullptr),
    _library_name()
{
    load(library_name);
}

r_dynamic_library::r_dynamic_library(r_dynamic_library&& obj) noexcept :
    _library_handle(obj._library_handle),
    _library_name(move(obj._library_name))
{
    obj._library_handle = nullptr;
}

r_dynamic_library::~r_dynamic_library() noexcept
{
    try
    {
        unload();
    }
    catch(...)
    {
    }
}

r_dynamic_library& r_dynamic_library::operator=(r_dynamic_library&& obj) noexcept
{
    if(this != &obj)
    {
        try
        {
            unload();
        }
        catch(...)
        {
        }
        
        _library_handle = obj._library_handle;
        _library_name = move(obj._library_name);
        obj._library_handle = nullptr;
    }
    
    return *this;
}

void r_dynamic_library::load(const string& library_name)
{
    if(_library_handle)
        R_THROW(("Dynamic library already loaded: %s", _library_name.c_str()));
    
#ifdef IS_WINDOWS
    _library_handle = LoadLibraryA(library_name.c_str());
    if(!_library_handle)
    {
        DWORD error = GetLastError();
        R_THROW(("Failed to load library '%s'. Windows error code: %lu", library_name.c_str(), error));
    }
#else
    _library_handle = dlopen(library_name.c_str(), RTLD_NOW);
    if(!_library_handle)
    {
        const char* error = dlerror();
        R_THROW(("Failed to load library '%s': %s", library_name.c_str(), (error ? error : "Unknown error")));
    }
#endif
    
    _library_name = library_name;
}

void* r_dynamic_library::resolve_symbol(const string& symbol_name) const
{
    if(!_library_handle)
        R_THROW(("Cannot resolve symbol: no library loaded"));
    
#ifdef IS_WINDOWS
    FARPROC symbol = GetProcAddress(_library_handle, symbol_name.c_str());
    if(!symbol)
    {
        DWORD error = GetLastError();
        R_THROW(("Failed to resolve symbol '%s' in library '%s'. Windows error code: %lu", 
            symbol_name.c_str(), _library_name.c_str(), error));
    }
    return reinterpret_cast<void*>(symbol);
#else
    dlerror(); // Clear any existing error
    void* symbol = dlsym(_library_handle, symbol_name.c_str());
    const char* error = dlerror();
    if(error)
    {
        R_THROW(("Failed to resolve symbol '%s' in library '%s': %s", 
            symbol_name.c_str(), _library_name.c_str(), error));
    }
    return symbol;
#endif
}

void r_dynamic_library::unload()
{
    if(_library_handle)
    {
#ifdef IS_WINDOWS
        if(!FreeLibrary(_library_handle))
        {
            DWORD error = GetLastError();
            R_THROW(("Failed to unload library '%s'. Windows error code: %lu", _library_name.c_str(), error));
        }
#else
        if(dlclose(_library_handle) != 0)
        {
            const char* error = dlerror();
            R_THROW(("Failed to unload library '%s': %s", _library_name.c_str(), (error ? error : "Unknown error")));
        }
#endif
        _library_handle = nullptr;
        _library_name.clear();
    }
}

bool r_dynamic_library::is_loaded() const
{
    return _library_handle != nullptr;
}

string r_dynamic_library::get_library_name() const
{
    return _library_name;
}