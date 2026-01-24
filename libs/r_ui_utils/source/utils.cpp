
#include "r_ui_utils/utils.h"
#include "r_utils/r_exception.h"
#include <cstring>

using namespace r_ui_utils;
using namespace std;

void r_ui_utils::copy_s(char* p, size_t len, const std::string& s)
{
#ifdef IS_WINDOWS
    auto res = strncpy_s(p, len, s.c_str(), (s.length()<len)?s.length():len);
    if(res != 0)
        R_THROW(("Unable to copy string to buffer."));
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
    strncpy(p, s.c_str(), (s.length()<len)?s.length():len);
    p[len-1] = '\0';  // Ensure null termination
#endif
}
