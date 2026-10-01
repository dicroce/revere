
#ifndef __vision_overlay_state_h
#define __vision_overlay_state_h

#include <string>
#include "r_utils/r_nullable.h"

namespace vision
{

// Saved placement + pin state for a single camera's overlay window. Persisted
// as one file per camera (config/overlay_<camera_id>.json) so each overlay
// process is the sole writer of its own file — no sharing or locking with the
// main window's vision_cfg.json (which is whole-file rewritten on save).
struct overlay_state
{
    int x {0};
    int y {0};
    int w {800};
    int h {520};
    bool pinned {false};

    bool operator==(const overlay_state& o) const
    {
        return x == o.x && y == o.y && w == o.w && h == o.h && pinned == o.pinned;
    }
};

// Null when the file is missing, unparseable, or holds an implausible size —
// callers fall back to the default centered placement.
r_utils::r_nullable<overlay_state> load_overlay_state(const std::string& camera_id);

// Best effort: a failed write is logged and otherwise ignored (the overlay
// keeps running; next launch just uses defaults).
void save_overlay_state(const std::string& camera_id, const overlay_state& os);

}

#endif
