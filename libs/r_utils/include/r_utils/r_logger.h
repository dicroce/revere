
#ifndef r_utils_r_logger_h
#define r_utils_r_logger_h

#include "r_utils/r_macro.h"
#include <cstdarg>
#include <cstdio>
#include <chrono>
#if defined(IS_LINUX) || defined(IS_MACOS)
#include <syslog.h>
#endif
#include <string>
#include <functional>

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

using log_callback_t = std::function<void(LOG_LEVEL level, const std::string& message)>;

// All mutable logger state lives here
struct logger_state
{
    FILE* log_file = nullptr;
    uint32_t approx_bytes_logged = 0;
    std::string log_dir = ".";
    std::string log_prefix = "log_";
    log_callback_t log_callback = nullptr;
    std::chrono::system_clock::time_point last_flush_time{};
    bool is_sandboxed = false;
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

R_API void set_log_callback(log_callback_t callback);
R_API void clear_log_callback();

R_API std::string get_log_file_path();

// Cross-DLL state sharing: host calls get_logger_state(), plugin calls set_logger_state()
R_API logger_state* get_logger_state();
R_API void set_logger_state(logger_state* state);

}

}

#endif
