#ifndef __r_ui_utils_window_placement_h
#define __r_ui_utils_window_placement_h

#include "r_utils/r_macro.h"
#include <optional>
#include <vector>

namespace r_ui_utils
{

// A window or display rectangle in logical points (SDL window coordinates).
struct window_rect
{
    int x {0};
    int y {0};
    int w {0};
    int h {0};

    bool operator==(const window_rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

// How much of a saved window has to be on a display to put it back there:
// enough of its top edge to grab and drag.
constexpr int MIN_VISIBLE_W = 100;
constexpr int MIN_VISIBLE_H = 50;

// Where to put a window saved at `saved`, given the usable bounds of the
// displays connected now. If at least MIN_VISIBLE_W x MIN_VISIBLE_H of it (or
// all of it, if it's smaller) is on a display, it goes on the display it
// overlaps most, shrunk to fit and moved fully inside, with `title_bar` points
// left above it for the frame. Otherwise (display unplugged, resolution
// changed, nothing saved) there's no saved place: the caller centers it.
R_API std::optional<window_rect> restore_window_rect(
    const window_rect& saved,
    const std::vector<window_rect>& displays,
    int title_bar = 0
);

}

#endif
