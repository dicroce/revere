
#ifndef r_utils_r_exception_h
#define r_utils_r_exception_h

#include "r_utils/r_logger.h"
#include "r_utils/r_macro.h"
#include "r_utils/r_string_utils.h"
#include <string>
#include <exception>
#include <assert.h>

namespace r_utils
{

class r_exception : public std::exception
{
public:
    R_API r_exception();
    R_API r_exception(const std::string& msg);
    R_API r_exception(const char* msg, ...);
    R_API virtual ~r_exception() noexcept;

    R_API void set_msg(const std::string& msg) { _msg = msg; }

    R_API virtual const char* what() const noexcept;

protected:
    mutable std::string _msg;
    std::string _stack;
};

class r_not_found_exception : public r_exception
{
public:
    R_API r_not_found_exception();
    R_API virtual ~r_not_found_exception() noexcept {}
    R_API r_not_found_exception(const char* msg, ...);
};

class r_invalid_argument_exception : public r_exception
{
public:
    R_API r_invalid_argument_exception();
    R_API virtual ~r_invalid_argument_exception() noexcept {}
    R_API r_invalid_argument_exception(const char* msg, ...);
};

class r_unauthorized_exception : public r_exception
{
public:
    R_API r_unauthorized_exception();
    R_API virtual ~r_unauthorized_exception() noexcept {}
    R_API r_unauthorized_exception(const char* msg, ...);
};

class r_not_implemented_exception : public r_exception
{
public:
    R_API r_not_implemented_exception();
    R_API virtual ~r_not_implemented_exception() noexcept {}
    R_API r_not_implemented_exception(const char* msg, ...);
};

class r_timeout_exception : public r_exception
{
public:
    R_API r_timeout_exception();
    R_API virtual ~r_timeout_exception() noexcept {}
    R_API r_timeout_exception(const char* msg, ...);
};

class r_io_exception : public r_exception
{
public:
    R_API r_io_exception();
    R_API virtual ~r_io_exception() noexcept {}
    R_API r_io_exception(const char* msg, ...);
};

class r_internal_exception : public r_exception
{
public:
    R_API r_internal_exception();
    R_API virtual ~r_internal_exception() noexcept {}
    R_API r_internal_exception(const char* msg, ...);
};

}

#define R_THROW(ARGS) \
R_MACRO_BEGIN \
    throw r_utils::r_exception ARGS ; \
R_MACRO_END

#define R_STHROW(EXTYPE, ARGS) \
R_MACRO_BEGIN \
    throw EXTYPE ARGS ; \
R_MACRO_END

#define R_LOG_EXCEPTION(E) \
R_MACRO_BEGIN \
    auto parts = r_utils::r_string_utils::split(std::string(E.what()), '\n'); \
    for( auto l : parts ) \
        R_LOG_ERROR("%s",l.c_str()); \
R_MACRO_END

#define R_LOG_EXCEPTION_AT(E, F, L) \
R_MACRO_BEGIN \
    auto parts = r_utils::r_string_utils::split(std::string(E.what()), '\n'); \
    for( auto l : parts ) \
        R_LOG_ERROR("%s:%d %s",F, L, l.c_str()); \
R_MACRO_END

#endif
