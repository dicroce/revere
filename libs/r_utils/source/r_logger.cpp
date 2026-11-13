
#include "r_utils/r_logger.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_file.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_crash_handler.h"
#include <exception>
#include <deque>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <condition_variable>
#include <thread>

#if defined(IS_LINUX) || defined(IS_MACOS)
#include <fnmatch.h>
#include <dirent.h>
#endif

#ifdef IS_WINDOWS
#include <io.h>
#include <fcntl.h>
#endif

using namespace r_utils;
using namespace std;

static const uint32_t MAX_LOG_FILE_SIZE = ((1024*1024) * 10);

// The actual state - owned by whichever module is the "host"
static r_logger::logger_state _owned_state;

// Pointer to the active state (points to _owned_state by default,
// but plugins will point this to the host's state)
static r_logger::logger_state* _state = &_owned_state;

// Console (stdout) output is written by a thread of its own. A terminal that
// stops reading (a paused or stalled terminal window, a full pipe) blocks a
// write to stdout, and while it's blocked so does every other write to stdout,
// which share its lock. Written inline, the first logging thread to hit the
// stall would block and then every thread that logs after it, which in revere
// is most of them (recording, the HTTP workers): the process froze with the
// terminal. Here only the console thread waits. Lines queue up to a bound and
// past it are dropped and counted; the count is printed once the terminal
// reads again. The log file isn't affected.
class console_writer
{
public:
    static const size_t MAX_QUEUED_BYTES = 256 * 1024;

    // Never destroyed: logging can happen during static destruction, and the
    // thread may be blocked in a write when the process exits.
    static console_writer& get()
    {
        static console_writer* writer = new console_writer();
        return *writer;
    }

    void post(string line)
    {
        {
            lock_guard<mutex> g(_lock);
            if(_queued_bytes + line.size() > MAX_QUEUED_BYTES)
            {
                ++_dropped;
                return;
            }
            _queued_bytes += line.size();
            _queue.push_back(std::move(line));
        }
        _cond.notify_one();
    }

    // Waits up to timeout for the queued lines to reach the terminal; false if
    // they didn't (it isn't reading).
    bool drain(chrono::milliseconds timeout)
    {
        unique_lock<mutex> g(_lock);
        return _drained.wait_for(g, timeout, [this](){ return _queue.empty() && _dropped == 0 && !_writing; });
    }

private:
    console_writer()
    {
        thread(&console_writer::_entry_point, this).detach();
    }

    void _entry_point()
    {
        while(true)
        {
            deque<string> lines;
            size_t dropped = 0;
            {
                unique_lock<mutex> g(_lock);
                _cond.wait(g, [this](){ return !_queue.empty() || _dropped > 0; });
                lines.swap(_queue);
                _queued_bytes = 0;
                dropped = _dropped;
                _dropped = 0;
                _writing = true;
            }

            if(dropped > 0)
                fprintf(stdout, "[%zu log line(s) not shown here: the terminal stopped reading. The log file has them.]\n", dropped);
            for(auto& l : lines)
                fputs(l.c_str(), stdout);
            fflush(stdout);

            {
                lock_guard<mutex> g(_lock);
                _writing = false;
            }
            _drained.notify_all();
        }
    }

    mutex _lock;
    condition_variable _cond;
    condition_variable _drained;
    bool _writing {false};
    deque<string> _queue;
    size_t _queued_bytes {0};
    size_t _dropped {0};
};

static deque<string> _get_file_names(const std::string& log_dir)
{
    deque<string> output;

#ifdef IS_WINDOWS
    wstring spec = r_string_utils::convert_multi_byte_string_to_wide_string(log_dir + "\\*.*");
    struct _wfinddata_t fs;
    intptr_t handle;
    if((handle=_wfindfirst(spec.c_str(), &fs)) == -1)
        R_THROW(("Unable to read directory!"));
    do
    {
        auto name = r_string_utils::convert_wide_string_to_multi_byte_string(fs.name);
        if(name != "." && name != "..")
            output.push_back(name);
    } while(0 == _wfindnext(handle, &fs));
    _findclose(handle);
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
    if(DIR* d = opendir(log_dir.c_str()))
    {
        while(struct dirent* d_entry = readdir(d))
        {
            if((strncmp(".", d_entry->d_name, 1) != 0) && (strncmp("..", d_entry->d_name, 2) != 0))
                output.push_back(string(d_entry->d_name));
        }
        closedir(d);
    }
    else
        R_THROW(("Unable to opendir() requested path."));
#endif

    return output;
}

static string _next_log_name(const string& log_dir, const string& log_prefix, bool use_last = false)
{
    auto file_names = _get_file_names(log_dir);

    file_names.erase(
        remove_if(
            begin(file_names),
            end(file_names),
            [&log_prefix](const string& filename)
            {
                return !r_string_utils::starts_with(filename, log_prefix);
            }
        ),
        end(file_names)
    );

    auto extract_num = [&log_prefix](const string& s) {
        auto inner = s.substr(log_prefix.length());
        auto dot = inner.rfind('.');
        return r_string_utils::s_to_uint32(dot != string::npos ? inner.substr(0, dot) : inner);
    };
    sort(begin(file_names), end(file_names), [&extract_num](const string& a, const string& b) {
        return extract_num(a) < extract_num(b);
    });

    auto n_file_names = file_names.size();

    if(file_names.size() >= 5)
    {
        auto remove_path = r_string_utils::format("%s%s%s",log_dir.c_str(),PATH_SLASH.c_str(),file_names.front().c_str());
        r_fs::remove_file(remove_path);
        file_names.pop_front();
    }

    if(n_file_names >= 1)
    {
        if(use_last)
            return file_names.back();

        auto newest_log_path = file_names.back();
        auto n = extract_num(newest_log_path);
        ++n;
        return log_prefix + r_string_utils::uint32_to_s(n) + ".txt";
    }
    else return log_prefix + "0" + ".txt";
}

// The log file can fail to open or to take a write: a full disk, or a log
// folder that's gone. Logging must never throw because of it (a throw out of
// write() in a thread nobody catches in is std::terminate, which is how a full
// disk used to crash revere at its next log rotation). Instead:
// - Lines that don't reach the file go to stdout (on macOS they go there
//   anyway), and are counted.
// - A failed open leaves the current file, if there is one, as it was, and is
//   tried again after _retry_interval, from whichever thread logs next.
// - Once a file takes lines again, it gets a line saying how many it missed.
//
// _file_lock guards the log file and the state below: a rotation closes the
// file, and another thread mustn't be writing to it then. Never destroyed, as
// logging can happen during static destruction.
static mutex& _file_lock()
{
    static mutex* m = new mutex();
    return *m;
}
static chrono::milliseconds _retry_interval = chrono::seconds(30);
static chrono::steady_clock::time_point _next_open_try {};
static bool _file_wanted = false;   // install_logger opened (or tried to open) a file
static bool _file_failing = false;  // the last open or write failed; noted once
static uint64_t _missed_lines = 0;  // lines that didn't reach the file since it last took them

static bool _uses_file_logging()
{
#if defined(IS_WINDOWS) || defined(IS_MACOS)
    return true;
#elif defined(IS_LINUX)
    return _state->is_sandboxed;
#else
    return false;
#endif
}

// Stdout gets the line itself only where it doesn't already (macOS logs every
// line to it).
static void _to_stdout(const string& line)
{
#ifndef IS_MACOS
    console_writer::get().post(line);
#else
    (void)line;
#endif
}

// A note on stdout, once per run of failures, that the file isn't being written.
static void _note_file_failure(const string& what)
{
    if(_file_failing)
        return;
    _file_failing = true;
    console_writer::get().post(
        "[The log file isn't being written (" + what + "). Log lines go to stdout until it is; "
        "trying again every " + r_string_utils::uint64_to_s((uint64_t)chrono::duration_cast<chrono::seconds>(_retry_interval).count()) + " s.]\n"
    );
}

// Opens the newest log file (rotate false) or the next one (rotate true), in
// place of the current one. Never throws. On failure the current file, if
// any, stays, and the next try waits for _retry_interval. Called with
// _file_lock held.
static bool _open_log_file(bool rotate)
{
    _next_open_try = chrono::steady_clock::now() + _retry_interval;

    string path, what;
    try
    {
        auto v = _next_log_name(_state->log_dir, _state->log_prefix, !rotate);
        path = r_string_utils::format("%s%s%s", _state->log_dir.c_str(), PATH_SLASH.c_str(), v.c_str());
    }
    catch(std::exception& ex)
    {
        // Its first line: an r_exception's what() goes on with a stack trace.
        string w = ex.what();
        what = _state->log_dir + ": " + w.substr(0, w.find('\n'));
    }
    catch(...) { what = _state->log_dir + ": unknown error"; }

    FILE* f = nullptr;
    if(!path.empty())
    {
#ifdef IS_WINDOWS
        f = _fsopen(path.c_str(), "a+", _SH_DENYNO);
#else
        f = fopen(path.c_str(), "a+");
#endif
        if(!f)
            what = path + ": " + strerror(errno);
    }

    if(!f)
    {
        _note_file_failure(what.empty() ? string("unknown error") : what);
        return false;
    }

    if(_state->log_file)
        fclose(_state->log_file);
    _state->log_file = f;

    long size = (fseek(f, 0, SEEK_END) == 0) ? ftell(f) : 0;
    _state->approx_bytes_logged = (size > 0) ? (uint32_t)size : 0;
    _file_failing = false;
    return true;
}

void r_utils::r_logger::write(LOG_LEVEL level,
                              int line,
                              const char* file,
                              const char* format,
                              ...)
{
    va_list args;
    va_start(args, format);
    r_logger::write(level, line, nullptr, format, args);
    va_end(args);
}

static void _write(r_logger::LOG_LEVEL level, const char* format, va_list& args)
{
    auto msg = r_string_utils::format(format, args);
    auto lines = r_string_utils::split(msg, "\n");

#if defined(IS_WINDOWS) || defined(IS_LINUX) || defined(IS_MACOS)
    auto now = std::chrono::system_clock::now();
    auto timestamp = r_time_utils::tp_to_iso_8601(now, false);
#endif

    // Invoke callback if registered
    if(_state->log_callback)
    {
        for(auto l : lines)
        {
#ifdef IS_WINDOWS
            auto timestamped_msg = timestamp + " " + l;
            _state->log_callback(level, timestamped_msg);
#elif defined(IS_LINUX)
            if(_state->is_sandboxed)
            {
                auto timestamped_msg = timestamp + " " + l;
                _state->log_callback(level, timestamped_msg);
            }
            else
            {
                _state->log_callback(level, l);
            }
#else
            _state->log_callback(level, l);
#endif
        }
    }

#if defined(IS_WINDOWS) || defined(IS_LINUX) || defined(IS_MACOS)
    // File logging for Windows, Linux in Flatpak/Snap, and macOS
    if(_uses_file_logging())
    {
        lock_guard<mutex> g(_file_lock());

        // Rotate past the size limit, or reopen after a failed open, once the
        // retry wait is over.
        bool over = _state->approx_bytes_logged > MAX_LOG_FILE_SIZE;
        bool missing = _file_wanted && !_state->log_file;
        if((over || missing) && chrono::steady_clock::now() >= _next_open_try)
            _open_log_file(over && _state->log_file);

        bool written = false;
        if(_state->log_file)
        {
            if(_missed_lines > 0)
                fprintf(_state->log_file, "%s [%llu log line(s) not written here: the log file couldn't be opened or written. They went to stdout.]\n",
                        timestamp.c_str(), (unsigned long long)_missed_lines);

            for(auto l : lines)
            {
                fprintf(_state->log_file, "%s %s\n", timestamp.c_str(), l.c_str());
            }

            // Flush every line. This used to batch non-error output on a 10 second
            // timer, which meant that when the process was killed outright (heap
            // corruption, abort, OS kill) the last few seconds of log — the only
            // part describing what led to the death — never reached disk. For a
            // service that is supposed to be recording unattended, being able to
            // see the run-up to a crash is worth more than the saved syscalls; the
            // steady-state log rate here is a handful of lines per minute.
            fflush(_state->log_file);
            _state->last_flush_time = now;

            // A full disk fails the write (or the flush). The error sticks to
            // the file until cleared; clear it so the next line tries again.
            if(ferror(_state->log_file))
            {
                int e = errno;
                clearerr(_state->log_file);
                _note_file_failure(string("write: ") + strerror(e));
            }
            else
            {
                written = true;
                _missed_lines = 0;
                _file_failing = false;
                _state->approx_bytes_logged += (uint32_t)msg.length();
            }
        }

        if(!written && (_file_wanted || _state->log_file))
        {
            _missed_lines += lines.size();
            for(auto l : lines)
                _to_stdout(timestamp + " " + l + "\n");
        }
    }
#endif

#ifdef IS_LINUX
    // Syslog for Linux when not in Flatpak
    if(!_state->is_sandboxed)
    {
        int priority = LOG_USER;

        switch(level)
        {
            case LOG_LEVEL_CRITICAL: { priority |= LOG_CRIT; break; }
            case LOG_LEVEL_ERROR: { priority |= LOG_ERR; break; }
            case LOG_LEVEL_WARNING: { priority |= LOG_WARNING; break; }
            case LOG_LEVEL_NOTICE: { priority |= LOG_NOTICE; break; }
            case LOG_LEVEL_INFO: { priority |= LOG_INFO; break; }
            case LOG_LEVEL_TRACE: { priority |= LOG_INFO; break; }
            case LOG_LEVEL_DEBUG: { priority |= LOG_INFO; break; }
            default: break;
        };

        for(auto l : lines)
        {
            syslog(priority, "%s\n", l.c_str());
        }
    }
#endif

#ifdef IS_MACOS
    for(auto l : lines)
        console_writer::get().post(timestamp + " " + l + "\n");
#endif
}

void r_utils::r_logger::write(LOG_LEVEL level,
                              int line,
                              const char* file,
                              const char* format,
                              va_list& args)
{
    // Never throws: a throw from here, in a thread that doesn't catch, would
    // be std::terminate.
    try
    {
        _write(level, format, args);
    }
    catch(std::exception& ex)
    {
        try { console_writer::get().post(string("[A log line was lost: ") + ex.what() + "]\n"); } catch(...) {}
    }
    catch(...)
    {
        try { console_writer::get().post("[A log line was lost.]\n"); } catch(...) {}
    }
}

void r_utils_terminate()
{
    // Not printf: with a terminal that isn't reading, stdout can be blocked
    // (and its lock held by the console thread), and the abort must happen.
    R_LOG_CRITICAL("r_utils terminate handler called!");

    // Capture a dump before we log-and-abort. An unhandled C++ exception
    // unwinds nothing useful by the time abort() runs, so without this the log
    // gets a message with no stack behind it. No-op unless the crash handler
    // was installed.
    r_crash_handler::write_dump_now("std::terminate");

    std::exception_ptr p = std::current_exception();

    if(p)
    {
        try
        {
            std::rethrow_exception(p);
        }
        catch(std::exception& ex)
        {
            R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
        }
        catch(...)
        {
            R_LOG_CRITICAL("unknown exception in r_utils_terminate().");
        }
    }

    // The last lines on their way to the terminal, briefly: the log file has
    // them anyway, and a terminal that isn't reading mustn't hold up the abort
    // (so no fflush(stdout) unless the console thread got through).
    if(console_writer::get().drain(chrono::milliseconds(100)))
        fflush(stdout);

    std::abort();
}

void r_utils::r_logger::install_logger(const std::string& log_dir, const std::string& log_prefix)
{
    _state->log_dir = log_dir;
    _state->log_prefix = log_prefix;

    // Auto-detect Flatpak or Snap environment (both need file logging)
    const char* flatpak_id = getenv("FLATPAK_ID");
    const char* snap = getenv("SNAP");
    _state->is_sandboxed = (flatpak_id != nullptr || snap != nullptr);

    // Open log file for Windows, Linux in Flatpak/Snap, and macOS. A failure
    // here doesn't throw either: lines go to stdout, and the file is tried
    // again later.
    if(_uses_file_logging())
    {
        lock_guard<mutex> g(_file_lock());
        _file_wanted = true;
        _open_log_file(false);
    }
}

void r_utils::r_logger::uninstall_logger()
{
    lock_guard<mutex> g(_file_lock());
    _file_wanted = false;
    _file_failing = false;
    _missed_lines = 0;
    _next_open_try = {};
    if(_state->log_file)
    {
        fclose(_state->log_file);
        _state->log_file = nullptr;
    }
}

void r_utils::r_logger::set_log_file_retry_interval(std::chrono::milliseconds interval)
{
    lock_guard<mutex> g(_file_lock());
    _retry_interval = interval;
    _next_open_try = {};
}

void r_utils::r_logger::install_terminate()
{
    set_terminate(r_utils_terminate);
}

void r_utils::r_logger::set_log_callback(log_callback_t callback)
{
    _state->log_callback = callback;
}

void r_utils::r_logger::clear_log_callback()
{
    _state->log_callback = nullptr;
}

std::string r_utils::r_logger::get_log_file_path()
{
    auto v = _next_log_name(_state->log_dir, _state->log_prefix, true);
    return r_string_utils::format("%s%s%s", _state->log_dir.c_str(), PATH_SLASH.c_str(), v.c_str());
}

r_logger::logger_state* r_utils::r_logger::get_logger_state()
{
    return _state;
}

void r_utils::r_logger::set_logger_state(logger_state* state)
{
    _state = state;
}
