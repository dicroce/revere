
#include "r_ui_utils/window_placement.h"
#include <algorithm>

using namespace std;

namespace r_ui_utils
{

static window_rect _intersect(const window_rect& a, const window_rect& b)
{
    int x1 = max(a.x, b.x), y1 = max(a.y, b.y);
    int x2 = min(a.x + a.w, b.x + b.w), y2 = min(a.y + a.h, b.y + b.h);
    return { x1, y1, max(0, x2 - x1), max(0, y2 - y1) };
}

optional<window_rect> restore_window_rect(const window_rect& saved, const vector<window_rect>& displays, int title_bar)
{
    if(saved.w <= 0 || saved.h <= 0)
        return nullopt;

    // The display showing the most of it, if enough of it shows to grab.
    const window_rect* best = nullptr;
    long long best_area = 0;
    for(const auto& d : displays)
    {
        if(d.w <= 0 || d.h <= 0)
            continue;
        auto in = _intersect(saved, d);
        if(in.w < min(MIN_VISIBLE_W, saved.w) || in.h < min(MIN_VISIBLE_H, saved.h))
            continue;
        long long area = (long long)in.w * in.h;
        if(area > best_area)
        {
            best = &d;
            best_area = area;
        }
    }
    if(!best)
        return nullopt;

    // Inside that display, frame included.
    const auto& d = *best;
    int top = d.y + max(0, min(title_bar, d.h / 2));
    window_rect out;
    out.w = min(saved.w, d.w);
    out.h = min(saved.h, d.y + d.h - top);
    out.x = clamp(saved.x, d.x, d.x + d.w - out.w);
    out.y = clamp(saved.y, top, d.y + d.h - out.h);
    return out;
}

}
