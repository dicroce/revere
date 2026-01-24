
#include "r_utils/r_std_utils.h"
#include <stdlib.h>
#include <stdio.h>

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
#endif
#ifdef IS_LINUX
    auto var = getenv(name.c_str());
    return (var)?string(var):string();
#endif
}