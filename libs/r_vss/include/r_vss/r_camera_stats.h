
#ifndef __r_vss_r_camera_stats_h
#define __r_vss_r_camera_stats_h

#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include "r_disco/r_camera.h"
#include "r_disco/r_devices.h"
#include "r_storage/r_md_storage_file_reader.h"
#include "r_vss/r_query.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace r_vss
{

// What the main window's camera pane shows about one camera, read from the
// camera's own storage (recording, motion and analytics files). Gathering it
// opens those files, so the UI does it off the frame loop and caches the result
// (see gather_camera_stats).
struct camera_stats
{
    // Disk space taken by the camera's recording, motion and analytics files.
    // The recording ring is allocated up front, so this is what the camera
    // occupies rather than how full it is.
    r_utils::r_nullable<uint64_t> storage_bytes;
    // How far back the recording goes (now - first timestamp).
    r_utils::r_nullable<std::chrono::seconds> kept;

    // Motion events since local midnight, and the same events by the hour they
    // started in (index 0 = midnight to 1am).
    int motion_events_today {0};
    std::array<int, 24> motion_by_hour {};

    // Motion events since midnight that saw each kind of object, most first,
    // e.g. {"people", 14}, {"vehicles", 3}.
    std::vector<std::pair<std::string, int>> detections_today;

    // Recorded time in the last 24 hours (or since the recording began, if
    // that's sooner), and the length of that window.
    std::chrono::seconds recorded_24h {0};
    std::chrono::seconds window_24h {0};

    // The start of the most recent motion event in the last 24 hours.
    r_utils::r_nullable<std::chrono::system_clock::time_point> last_motion;
};

// When each camera's stream was lost and restarted, kept in memory for a day.
// revere doesn't otherwise record reconnects, so the count covers only the time
// since revere started (started_at). Thread-safe.
class reconnect_log
{
public:
    R_API reconnect_log(std::chrono::system_clock::time_point started_at = std::chrono::system_clock::now());

    R_API void record(const std::string& camera_id, std::chrono::system_clock::time_point at = std::chrono::system_clock::now());
    R_API size_t count_since(const std::string& camera_id, std::chrono::system_clock::time_point since) const;
    std::chrono::system_clock::time_point started_at() const { return _started_at; }

private:
    mutable std::mutex _mtx;
    std::map<std::string, std::deque<std::chrono::system_clock::time_point>> _times;
    std::chrono::system_clock::time_point _started_at;
};

// The selected camera's stats for a frame loop: poll() starts a read on a
// background thread when the last one is older than the interval and none is
// running, and never blocks; get() returns the last result for a camera (null
// before its first). Only one read runs at a time.
class camera_stats_cache
{
public:
    using gather_fn = std::function<camera_stats(const r_disco::r_camera&)>;

    R_API camera_stats_cache(gather_fn gather, std::chrono::steady_clock::duration interval = std::chrono::seconds(5));
    // Waits for a read in progress, so destroy this before what gather uses.
    R_API ~camera_stats_cache() noexcept;

    // True when a read finished since the last call, so the caller redraws.
    R_API bool poll(const r_utils::r_nullable<r_disco::r_camera>& selected, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    R_API std::shared_ptr<const camera_stats> get(const std::string& camera_id) const;

private:
    gather_fn _gather;
    std::chrono::steady_clock::duration _interval;
    std::map<std::string, std::shared_ptr<const camera_stats>> _stats;
    std::map<std::string, std::chrono::steady_clock::time_point> _started;
    std::string _pending_id;
    std::future<camera_stats> _pending;
};

// The pieces gather_camera_stats is made of, separate so they can be tested
// without storage files.

// Counts events by the hour of day they started in, for events that started
// at or after day_start (local midnight) and before day_start + 24 h.
R_API std::array<int, 24> motion_hour_buckets(const std::vector<motion_event_info>& events, std::chrono::system_clock::time_point day_start);

// The kind of object a detection's class_name is, as the pane groups them:
// "people", "vehicles", "animals", or the class name itself for anything else.
R_API std::string detection_group(const std::string& class_name);

// For each analytics entry (one per motion event, as the detection plugin
// writes them: {"analytics": {"detections": [{"class_name": …}, …]}}), counts
// the groups it saw once each, then sorts by count (ties by name). Entries
// that don't parse are skipped.
R_API std::vector<std::pair<std::string, int>> count_detections(const std::vector<r_storage::r_metadata_entry>& entries);

// The time the segments cover inside [start, end).
R_API std::chrono::seconds covered_time(const std::vector<segment>& segments, std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end);

// The camera's storage files that exist: its recording (.nts), the analytics
// store beside it (.mdnts and its sqlite files) and the motion files (.mdb, .db).
R_API std::vector<std::string> camera_storage_files(const std::string& top_dir, const r_disco::r_camera& camera);

// Reads it all for one camera. Best-effort: anything that can't be read is
// left empty. Never throws.
R_API camera_stats gather_camera_stats(
    const std::string& top_dir,
    r_disco::r_devices& devices,
    const r_disco::r_camera& camera,
    std::chrono::system_clock::time_point now,
    std::chrono::system_clock::time_point day_start
);

// Local midnight at the start of the day `now` is in.
R_API std::chrono::system_clock::time_point local_day_start(std::chrono::system_clock::time_point now);

// Display text for the pane's tiles.
R_API std::string format_storage(uint64_t bytes);                         // "212 GB", "840 MB"
R_API std::string format_ago(std::chrono::seconds ago);                   // "just now", "4 min ago", "3 h ago"
R_API std::string format_detection_count(const std::string& group, int n);   // "7 people", "1 vehicle", "3 animals"
R_API std::string format_detections(const std::vector<std::pair<std::string, int>>& counts, size_t max_groups = 3); // "14 people · 3 vehicles · 1 animal"

}

#endif
