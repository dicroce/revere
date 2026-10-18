
#ifndef __r_vss_r_detection_pass_h
#define __r_vss_r_detection_pass_h

#include "r_vss/r_detection_planner.h"
#include "r_vss/r_query.h"
#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace r_vss
{

// Runs the detection pass over camera-reported motion on one thread: the
// planner decides which frame to look at when; this fetches the frame (the
// newest live key frame, or a stored one by time), runs object detection on
// it, and keeps each finished session's result.
//
// Milestone 4 runs it in shadow mode: results are logged and kept in memory
// (newest MAX_RESULTS) for comparison with today's MOG2 + YOLO; nothing is
// written to storage or put on the detection bus.
class r_detection_pass
{
public:
    // A frame for a look: live (ts ignored, the newest key frame) or stored
    // (the key frame at or before ts). Null if there is none.
    using frame_fn = std::function<r_utils::r_nullable<r_letterboxed_frame>(const std::string& camera_id, int64_t ts)>;
    // Object detection on a frame; false if it couldn't run.
    using detect_fn = std::function<bool(const std::string& camera_id, const r_letterboxed_frame& frame, std::vector<r_detection>& out)>;
    // A camera's display name, for the log.
    using name_fn = std::function<std::string(const std::string& camera_id)>;

    enum
    {
        MAX_RESULTS = 500,
        LIVE_MAX_AGE_MS = 10000,     // a live key frame older than this: the camera has stalled
        STORED_MAX_GAP_MS = 10000    // a stored key frame this far before the time wanted: not written yet, or a gap
    };

    R_API r_detection_pass(frame_fn live, frame_fn stored, detect_fn detect, name_fn name);
    R_API ~r_detection_pass() noexcept;

    r_detection_pass(const r_detection_pass&) = delete;
    r_detection_pass& operator=(const r_detection_pass&) = delete;

    R_API void start();
    R_API void stop();

    // From a camera's event subscription.
    R_API void motion(const std::string& camera_id, const std::string& source, bool active);
    R_API void camera_class(const std::string& camera_id, const std::string& what);

    // Finished sessions for a camera that started at or after since_ms, oldest first.
    R_API std::vector<r_detection_session> results(const std::string& camera_id, int64_t since_ms) const;

private:
    void _entry_point();
    void _log_first_look(const r_look& look, const std::vector<r_detection>& dets);
    void _log_session(const r_detection_session& s);

    frame_fn _live;
    frame_fn _stored;
    detect_fn _detect;
    name_fn _name;

    mutable std::mutex _lock;
    std::condition_variable _cv;
    r_detection_planner _planner;
    bool _kick {false};  // something changed: plan again
    std::deque<r_detection_session> _results;
    std::atomic<bool> _running {false};
    std::thread _th;
};

}

#endif
