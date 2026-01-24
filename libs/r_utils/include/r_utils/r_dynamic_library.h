#ifndef r_utils_r_dynamic_library_h
#define r_utils_r_dynamic_library_h

#include "r_utils/r_macro.h"
#include "r_utils/r_exception.h"
#include <string>

#ifdef IS_WINDOWS
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace r_utils
{

class r_dynamic_library final
{
public:
    R_API r_dynamic_library();
    R_API explicit r_dynamic_library(const std::string& library_name);
    R_API r_dynamic_library(r_dynamic_library&& obj) noexcept;
    R_API r_dynamic_library(const r_dynamic_library&) = delete;
    R_API ~r_dynamic_library() noexcept;

    R_API r_dynamic_library& operator=(r_dynamic_library&& obj) noexcept;
    R_API r_dynamic_library& operator=(const r_dynamic_library&) = delete;

    R_API void load(const std::string& library_name);
    R_API void* resolve_symbol(const std::string& symbol_name) const;
    R_API void unload();
    R_API bool is_loaded() const;
    R_API std::string get_library_name() const;

private:
#ifdef IS_WINDOWS
    HINSTANCE _library_handle;
#else
    void* _library_handle;
#endif
    std::string _library_name;
};

}

#endif