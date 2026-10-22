#ifndef __r_ui_utils_log_tail_h
#define __r_ui_utils_log_tail_h

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace r_ui_utils
{

// The last lines the logger wrote, for a log pane. add() may be called from
// any thread (it's installed as the logger callback); the UI reads under the
// lock. That lock isn't recursive and add() takes it, so nothing called from a
// with_lines() callback may log.
class log_tail
{
public:
    explicit log_tail(size_t max_lines = 1000) : _max_lines(max_lines) {}

    void add(const std::string& line)
    {
        std::lock_guard<std::mutex> g(_mtx);
        _lines.push_back(line);
        while(_lines.size() > _max_lines)
        {
            _lines.pop_front();
            ++_first;
        }
    }

    // cb(lines, first): first is the number of the front line, counted from
    // the first line ever added, so a selection keeps to its text as old lines
    // drop off the front.
    template<typename CB>
    void with_lines(CB cb) const
    {
        std::lock_guard<std::mutex> g(_mtx);
        cb(_lines, _first);
    }

private:
    mutable std::mutex _mtx;
    std::deque<std::string> _lines;
    uint64_t _first {0};
    size_t _max_lines;
};

}

#endif
