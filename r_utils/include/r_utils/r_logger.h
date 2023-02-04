
#ifndef r_utils_r_logger_h
#define r_utils_r_logger_h

#include "r_utils/r_macro.h"
#include <cstdarg>
#ifdef IS_LINUX
#include <syslog.h>
#endif
#include <string>

namespace r_utils
{

namespace r_logger
{

enum LOG_LEVEL
{
    LOG_LEVEL_CRITICAL = 1,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_WARNING = 4,
    LOG_LEVEL_NOTICE = 5,
    LOG_LEVEL_INFO = 6,
    LOG_LEVEL_TRACE = 7,
    LOG_LEVEL_DEBUG = 8
};

#define R_LOG_CRITICAL(format, ...) r_utils::r_logger::write(r_utils::r_logger::LOG_LEVEL_CRITICAL, __LINE__, __FILE__, format,  ##__VA_ARGS__)
#define R_LOG_ERROR(format, ...) r_utils::r_logger::write(r_utils::r_logger::LOG_LEVEL_ERROR, __LINE__, __FILE__, format,  ##__VA_ARGS__)
#define R_LOG_WARNING(format, ...) r_utils::r_logger::write(r_utils::r_logger::LOG_LEVEL_WARNING, __LINE__, __FILE__, format,  ##__VA_ARGS__)
#define R_LOG_NOTICE(format, ...) r_utils::r_logger::write(r_utils::r_logger::LOG_LEVEL_NOTICE, __LINE__, __FILE__, format,  ##__VA_ARGS__)
#define R_LOG_INFO(format, ...) r_utils::r_logger::write(r_utils::r_logger::LOG_LEVEL_INFO, __LINE__, __FILE__, format,  ##__VA_ARGS__)
#define R_LOG_TRACE(format, ...) r_utils::r_logger::write(r_utils::r_logger::LOG_LEVEL_TRACE, __LINE__, __FILE__, format,  ##__VA_ARGS__)
#define R_LOG_DEBUG(format, ...) r_utils::r_logger::write(r_utils::r_logger::LOG_LEVEL_DEBUG, __LINE__, __FILE__, format,  ##__VA_ARGS__)

R_API void write(LOG_LEVEL level, int line, const char* file, const char* format, ...);
R_API void write(LOG_LEVEL level, int line, const char* file, const char* format, va_list& args);

R_API void install_logger(const std::string& log_dir, const std::string& log_prefix);
R_API void uninstall_logger();
R_API void install_terminate();

}

}

#endif
