
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_stack_trace.h"

using namespace std;
using namespace r_utils;

r_exception::r_exception() :
    exception(),
    _msg(),
    _stack(r_stack_trace::get_stack())
{
}

r_exception::r_exception(const string& msg) :
    exception(),
    _msg(msg),
    _stack(r_stack_trace::get_stack())
{
}

r_exception::r_exception(const char* msg, ...) : 
    exception(),
    _msg(),
    _stack(r_stack_trace::get_stack())
{
    va_list args;
    va_start(args, msg);
    _msg = r_string_utils::format(msg, args);
    va_end(args);
}

r_exception::~r_exception() noexcept
{
}

const char* r_exception::what() const noexcept
{
    // XXX Note: Since this method returns a char*, we MUST assign our _msg
    // member to the new string to guarantee the lifetime of the pointer to
    // be as long as this r_exception itself.
    _msg = r_string_utils::format("%s\n%s", _msg.c_str(), _stack.c_str());
    return _msg.c_str();
}

r_not_found_exception::r_not_found_exception() :
    r_exception()
{
}

r_not_found_exception::r_not_found_exception(const char* msg, ...) :
    r_exception()
{
    va_list args;
    va_start(args, msg);
    set_msg(r_string_utils::format(msg, args));
    va_end(args);
}

r_invalid_argument_exception::r_invalid_argument_exception() :
    r_exception()
{
}

r_invalid_argument_exception::r_invalid_argument_exception(const char* msg, ...) :
    r_exception()
{
    va_list args;
    va_start(args, msg);
    set_msg(r_string_utils::format(msg, args));
    va_end(args);
}

r_unauthorized_exception::r_unauthorized_exception() :
    r_exception()
{
}

r_unauthorized_exception::r_unauthorized_exception(const char* msg, ...) :
    r_exception()
{
    va_list args;
    va_start(args, msg);
    set_msg(r_string_utils::format(msg, args));
    va_end(args);
}

r_not_implemented_exception::r_not_implemented_exception() :
    r_exception()
{
}

r_not_implemented_exception::r_not_implemented_exception(const char* msg, ...) :
    r_exception()
{
    va_list args;
    va_start(args, msg);
    set_msg(r_string_utils::format(msg, args));
    va_end(args);
}

r_timeout_exception::r_timeout_exception() :
    r_exception()
{
}

r_timeout_exception::r_timeout_exception(const char* msg, ...) :
    r_exception()
{
    va_list args;
    va_start(args, msg);
    set_msg(r_string_utils::format(msg, args));
    va_end(args);
}

r_io_exception::r_io_exception() :
    r_exception()
{
}

r_io_exception::r_io_exception(const char* msg, ...) :
    r_exception()
{
    va_list args;
    va_start(args, msg);
    set_msg(r_string_utils::format(msg, args));
    va_end(args);
}

r_internal_exception::r_internal_exception() :
    r_exception()
{
}

r_internal_exception::r_internal_exception(const char* msg, ...) :
    r_exception()
{
    va_list args;
    va_start(args, msg);
    set_msg(r_string_utils::format(msg, args));
    va_end(args);
}
