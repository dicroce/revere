
#include "r_ui_utils/utils.h"
#include "r_utils/r_exception.h"
#include <cstring>

using namespace r_ui_utils;
using namespace std;

void r_ui_utils::copy_s(char* p, size_t len, const std::string& s)
{
    // Truncates to fit; always terminated.
    if(len == 0)
        return;
    size_t n = (s.length() < len) ? s.length() : len - 1;
    memcpy(p, s.data(), n);
    p[n] = '\0';
}
