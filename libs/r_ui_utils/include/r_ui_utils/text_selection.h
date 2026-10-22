#ifndef __r_ui_utils_text_selection_h
#define __r_ui_utils_text_selection_h

#include "r_utils/r_macro.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

namespace r_ui_utils
{

// A place in a list of text lines: a line number and a byte offset in it.
// Line numbers count from the first line ever added, not from the front of
// the list, so a selection stays on the same text while old lines scroll out
// of a log that keeps only its last N lines.
struct text_pos
{
    uint64_t line {0};
    size_t col {0};
};

inline bool operator<(const text_pos& a, const text_pos& b)
{
    return a.line < b.line || (a.line == b.line && a.col < b.col);
}

inline bool operator==(const text_pos& a, const text_pos& b)
{
    return a.line == b.line && a.col == b.col;
}

// A selection over lines of read-only text, made by dragging: begin() where
// the mouse went down, extend() as it moves. Either end can come first.
class text_selection
{
public:
    void begin(const text_pos& p) { _anchor = _cursor = p; _set = true; }
    void extend(const text_pos& p) { if(_set) _cursor = p; }
    void select(const text_pos& from, const text_pos& to) { _anchor = from; _cursor = to; _set = true; }
    void clear() { _set = false; }

    bool empty() const { return !_set || _anchor == _cursor; }
    text_pos start() const { return (_cursor < _anchor) ? _cursor : _anchor; }
    text_pos end() const { return (_cursor < _anchor) ? _anchor : _cursor; }

    // The bytes [from, to) of a line len bytes long that are selected; false if
    // none are. eol is true when the selection carries on past the line's end
    // (the line break is selected too).
    R_API bool line_span(uint64_t line, size_t len, size_t& from, size_t& to, bool& eol) const;

private:
    text_pos _anchor;
    text_pos _cursor;
    bool _set {false};
};

// The selected text of lines, whose front is line number first, with "\n"
// between lines. Lines the selection covers that have already left the front
// of lines are skipped, and columns past a line's end are clamped to it.
R_API std::string selected_text(const text_selection& sel, const std::deque<std::string>& lines, uint64_t first);

// All of lines, one per line.
R_API std::string all_text(const std::deque<std::string>& lines);

// The byte offset in line nearest to x, where width(begin, end) measures the
// bytes [begin, end) as drawn. Never splits a UTF-8 sequence: the offset is
// always at the start of a character, or at the end of the line.
R_API size_t col_at_x(const std::string& line, float x, const std::function<float(const char*, const char*)>& width);

}

#endif
