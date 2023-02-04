
#ifndef r_utils_r_stack_trace_h
#define r_utils_r_stack_trace_h

#include "r_utils/r_macro.h"
#include <string>
#include <vector>

namespace r_utils
{

namespace r_stack_trace
{

R_API std::string get_stack(char sep = '\n');

}

}

#endif
