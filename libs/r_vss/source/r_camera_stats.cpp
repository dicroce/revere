
#include "r_vss/r_camera_stats.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/3rdparty/json/json.h"
#include <algorithm>
#include <ctime>
#include <map>
#include <set>

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace std::chrono;
using json = nlohmann::json;

// The same rule r_query uses: a bare file name is in <top_dir>/video.
static string _storage_path(const string& file_path, const string& top_dir)
{
    if(file_path.find('/') != string::npos || file_path.find('\\') != string::npos)
        return file_path;
    return top_dir + PATH_SLASH + "video" + PATH_SLASH + file_path;
}

static string _without_suffix(const string& s, const string& suffix)
{
    if(s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
        return s.substr(0, s.size() - suffix.size());
    return s;
}

r_vss::reconnect_log::reconnect_log(system_clock::time_point started_at) :
    _started_at(started_at)
{
}

void r_vss::reconnect_log::record(const string& camera_id, system_clock::time_point at)
{
    lock_guard<mutex> g(_mtx);
    auto& times = _times[camera_id];
    times.push_back(at);
    while(!times.empty() && times.front() < at - hours(24))
        times.pop_front();
}

size_t r_vss::reconnect_log::count_since(const string& camera_id, system_clock::time_point since) const
{
    lock_guard<mutex> g(_mtx);
    auto it = _times.find(camera_id);
    if(it == _times.end())
        return 0;
    return (size_t)count_if(it->second.begin(), it->second.end(), [&](auto t) { return t >= since; });
}

r_vss::camera_stats_cache::camera_stats_cache(gather_fn gather, steady_clock::duration interval) :
    _gather(std::move(gather)),
    _interval(interval)
{
}

r_vss::camera_stats_cache::~camera_stats_cache() noexcept
{
    try
    {
        if(_pending.valid())
            _pending.wait();
    }
    catch(...) {}
}

bool r_vss::camera_stats_cache::poll(const r_nullable<r_disco::r_camera>& selected, steady_clock::time_point now)
{
    bool finished = false;
    if(_pending.valid() && _pending.wait_for(seconds(0)) == future_status::ready)
    {
        try
        {
            _stats[_pending_id] = make_shared<const camera_stats>(_pending.get());
        }
        catch(const exception&)
        {
            // Keep the last result; the next poll tries again.
        }
        finished = true;
    }

    if(!_pending.valid() && !selected.is_null())
    {
        const auto camera = selected.value();
        auto it = _started.find(camera.id);
        if(it == _started.end() || now - it->second >= _interval)
        {
            _started[camera.id] = now;
            _pending_id = camera.id;
            auto gather = _gather;
            _pending = std::async(std::launch::async, [gather, camera]() { return gather(camera); });
        }
    }

    return finished;
}

shared_ptr<const camera_stats> r_vss::camera_stats_cache::get(const string& camera_id) const
{
    auto it = _stats.find(camera_id);
    return it == _stats.end() ? nullptr : it->second;
}

array<int, 24> r_vss::motion_hour_buckets(const vector<motion_event_info>& events, system_clock::time_point day_start)
{
    array<int, 24> buckets {};
    for(const auto& e : events)
    {
        if(e.start < day_start)
            continue;
        auto hour = duration_cast<hours>(e.start - day_start).count();
        if(hour < 24)
            ++buckets[(size_t)hour];
    }
    return buckets;
}

string r_vss::detection_group(const string& class_name)
{
    // The groups r_ui_utils::theme::detection_class_color colours alike.
    static const set<string> vehicles {"bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat"};
    static const set<string> animals {"bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe"};
    if(class_name == "person")
        return "person";
    if(vehicles.count(class_name))
        return "vehicle";
    if(animals.count(class_name))
        return "animal";
    return class_name;
}

vector<pair<string, int>> r_vss::count_detections(const vector<r_storage::r_metadata_entry>& entries)
{
    map<string, int> counts;
    for(const auto& entry : entries)
    {
        try
        {
            auto j = json::parse(entry.json_data);
            const auto& a = j.contains("analytics") ? j["analytics"] : j;
            if(!a.contains("detections") || !a["detections"].is_array())
                continue;
            set<string> seen;
            for(const auto& d : a["detections"])
                if(d.contains("class_name") && d["class_name"].is_string())
                    seen.insert(detection_group(d["class_name"].get<string>()));
            for(const auto& g : seen)
                ++counts[g];
        }
        catch(const exception&)
        {
            // Not a detection entry.
        }
    }

    vector<pair<string, int>> out(counts.begin(), counts.end());
    stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}

seconds r_vss::covered_time(const vector<segment>& segments, system_clock::time_point start, system_clock::time_point end)
{
    // Segments can overlap (a new one starts before the last is closed), so
    // merge them before adding up.
    vector<pair<system_clock::time_point, system_clock::time_point>> spans;
    for(const auto& s : segments)
    {
        auto a = max(s.start, start);
        auto b = min(s.end, end);
        if(b > a)
            spans.push_back({a, b});
    }
    sort(spans.begin(), spans.end());

    system_clock::duration total {0};
    system_clock::time_point reach = start;
    for(const auto& s : spans)
    {
        auto a = max(s.first, reach);
        if(s.second > a)
        {
            total += s.second - a;
            reach = s.second;
        }
    }
    return duration_cast<seconds>(total);
}

vector<string> r_vss::camera_storage_files(const string& top_dir, const r_disco::r_camera& camera)
{
    vector<string> candidates;
    if(!camera.record_file_path.is_null())
    {
        auto nts = _storage_path(camera.record_file_path.value(), top_dir);
        candidates.push_back(nts);
        // The analytics store sits beside the recording (see query_get_analytics).
        auto md = _without_suffix(nts, ".nts") + ".mdnts";
        for(const auto& suffix : {"", ".db", ".db-shm", ".db-wal"})
            candidates.push_back(md + suffix);
    }
    if(!camera.motion_detection_file_path.is_null())
    {
        auto base = _without_suffix(_storage_path(camera.motion_detection_file_path.value(), top_dir), ".mdb");
        candidates.push_back(base + ".mdb");
        candidates.push_back(base + ".db");
    }

    vector<string> out;
    for(const auto& c : candidates)
        if(find(out.begin(), out.end(), c) == out.end() && r_fs::file_exists(c))
            out.push_back(c);
    return out;
}

camera_stats r_vss::gather_camera_stats(
    const string& top_dir,
    r_disco::r_devices& devices,
    const r_disco::r_camera& camera,
    system_clock::time_point now,
    system_clock::time_point day_start
)
{
    camera_stats out;

    try
    {
        uint64_t bytes = 0;
        auto files = camera_storage_files(top_dir, camera);
        for(const auto& f : files)
            bytes += r_fs::file_size(f);
        if(!files.empty())
            out.storage_bytes.set_value(bytes);
    }
    catch(const exception&) {}

    const auto window_start = now - hours(24);

    try
    {
        if(!camera.record_file_path.is_null())
        {
            auto c = query_get_contents(top_dir, devices, camera.id, window_start, now);
            if(!c.first_ts.is_null())
            {
                out.kept.set_value(duration_cast<seconds>(now - c.first_ts.value()));
                auto from = max(window_start, c.first_ts.value());
                out.window_24h = duration_cast<seconds>(now - from);
                out.recorded_24h = covered_time(c.segments, from, now);
            }
        }
    }
    catch(const exception&) {}

    try
    {
        if(!camera.motion_detection_file_path.is_null())
        {
            // One byte a second for the last day, so cheap to read.
            auto events = query_get_motion_events(top_dir, devices, camera.id, window_start, now);
            out.motion_by_hour = motion_hour_buckets(events, day_start);
            for(int n : out.motion_by_hour)
                out.motion_events_today += n;
            for(const auto& e : events)
                if(out.last_motion.is_null() || e.start > out.last_motion.value())
                    out.last_motion.set_value(e.start);
        }
    }
    catch(const exception&) {}

    try
    {
        if(!camera.record_file_path.is_null())
            out.detections_today = count_detections(query_get_analytics(top_dir, devices, camera.id, day_start, now));
    }
    catch(const exception&) {}

    return out;
}

system_clock::time_point r_vss::local_day_start(system_clock::time_point now)
{
    time_t t = system_clock::to_time_t(now);
    tm local {};
#ifdef IS_WINDOWS
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    return system_clock::from_time_t(mktime(&local));
}

string r_vss::format_storage(uint64_t bytes)
{
    const double kb = 1000.0, mb = kb * 1000.0, gb = mb * 1000.0, tb = gb * 1000.0;
    double b = (double)bytes;
    if(b >= tb)
        return r_string_utils::format("%.1f TB", b / tb);
    if(b >= gb)
        return r_string_utils::format("%.0f GB", b / gb);
    if(b >= mb)
        return r_string_utils::format("%.0f MB", b / mb);
    return r_string_utils::format("%.0f KB", b / kb);
}

string r_vss::format_ago(seconds ago)
{
    auto s = ago.count();
    if(s < 60)
        return "just now";
    if(s < 3600)
        return r_string_utils::format("%lld min ago", (long long)(s / 60));
    if(s < 86400)
        return r_string_utils::format("%lld h ago", (long long)(s / 3600));
    return r_string_utils::format("%lld d ago", (long long)(s / 86400));
}

string r_vss::format_detection_count(const string& group, int n)
{
    string name = group;
    if(n != 1)
    {
        if(group == "person")
            name = "people";
        else if(group.empty() || group.back() != 's')
            name = group + "s";
    }
    return r_string_utils::format("%d ", n) + name;
}

string r_vss::format_detections(const vector<pair<string, int>>& counts, size_t max_groups)
{
    string out;
    for(size_t i = 0; i < counts.size() && i < max_groups; ++i)
    {
        if(!out.empty())
            out += " \xC2\xB7 ";
        out += format_detection_count(counts[i].first, counts[i].second);
    }
    return out;
}
