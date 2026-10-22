#include "r_ui_utils/text_selection.h"
#include <algorithm>

using namespace r_ui_utils;
using namespace std;

bool text_selection::line_span(uint64_t line, size_t len, size_t& from, size_t& to, bool& eol) const
{
    if(empty())
        return false;
    auto s = start(), e = end();
    if(line < s.line || line > e.line)
        return false;
    from = (line == s.line) ? min(s.col, len) : 0;
    to = (line == e.line) ? min(e.col, len) : len;
    eol = line < e.line;
    return to > from || eol;
}

string r_ui_utils::selected_text(const text_selection& sel, const deque<string>& lines, uint64_t first)
{
    string out;
    if(sel.empty() || lines.empty())
        return out;
    auto s = sel.start(), e = sel.end();
    auto last = first + lines.size() - 1;
    auto from_line = max(s.line, first);
    auto to_line = min(e.line, last);
    for(auto line = from_line; line <= to_line && from_line <= to_line; ++line)
    {
        const auto& text = lines[(size_t)(line - first)];
        size_t from, to;
        bool eol;
        if(sel.line_span(line, text.size(), from, to, eol))
            out.append(text, from, to - from);
        if(line < to_line)
            out += '\n';
    }
    return out;
}

string r_ui_utils::all_text(const deque<string>& lines)
{
    size_t n = 0;
    for(const auto& l : lines)
        n += l.size() + 1;
    string out;
    out.reserve(n);
    for(const auto& l : lines)
    {
        out += l;
        out += '\n';
    }
    return out;
}

size_t r_ui_utils::col_at_x(const string& line, float x, const function<float(const char*, const char*)>& width)
{
    const char* b = line.data();
    size_t i = 0;
    float left = 0.0f;
    while(i < line.size())
    {
        // The next character: a lead byte and its continuation bytes.
        size_t next = i + 1;
        while(next < line.size() && ((unsigned char)line[next] & 0xC0) == 0x80)
            ++next;
        float w = width(b + i, b + next);
        // Left of this character's middle: the gap before it.
        if(x < left + w * 0.5f)
            return i;
        left += w;
        i = next;
    }
    return line.size();
}
