
#include "r_utils/r_std_utils.h"
#include <stdlib.h>
#include <stdio.h>
#ifdef IS_WINDOWS
#include <windows.h>
#else
#include <pthread.h>
#endif

using namespace std;

string r_utils::r_std_utils::get_env(const string& name)
{
#ifdef IS_WINDOWS
   char* var = nullptr;
   size_t len;
   errno_t err = _dupenv_s(&var, &len, name.c_str());
   auto result = (err==0 && var)?string(var):string();
   if(var)
      free(var);
   return result;
#else
    // Linux and macOS (it used to cover only IS_LINUX, so on macOS it fell
    // off the end without returning).
    auto var = getenv(name.c_str());
    return (var)?string(var):string();
#endif
}

void r_utils::r_std_utils::set_env(const string& name, const string& value)
{
#ifdef IS_WINDOWS
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

void r_utils::r_std_utils::unset_env(const string& name)
{
#ifdef IS_WINDOWS
    // An empty value removes it.
    _putenv_s(name.c_str(), "");
#else
    unsetenv(name.c_str());
#endif
}

void r_utils::r_std_utils::set_thread_name(const string& name)
{
#if defined(IS_WINDOWS)
    wstring wname(name.begin(), name.end());
    SetThreadDescription(GetCurrentThread(), wname.c_str());
#elif defined(IS_MACOS)
    pthread_setname_np(name.c_str());
#else
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
#endif
}
