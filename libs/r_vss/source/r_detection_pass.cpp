#include "r_vss/r_detection_pass.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_time_utils.h"
#include <algorithm>
#include <chrono>

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

static int64_t _now_ms()
{
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static bool _is_live(r_look_kind kind)
{
    return kind == look_first || kind == look_follow_up;
}

// "person 87%, car 81%": each class once, at its best confidence, best first.
static string _summary(const vector<r_detection>& dets, bool with_counts)
{
    map<string, pair<float, int>> best;
    for (auto& d : dets)
    {
        auto& b = best[d.class_name];
        b.first = max(b.first, d.confidence);
        ++b.second;
    }
    vector<pair<string, pair<float, int>>> sorted(best.begin(), best.end());
    sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.first > b.second.first; });

    string out;
    for (auto& s : sorted)
    {
        if (!out.empty())
            out += ", ";
        out += s.first + " " + to_string((int)(s.second.first * 100.0f + 0.5f)) + "%";
        if (with_counts)
            out += " (" + to_string(s.second.second) + ")";
    }
    return out.empty() ? string("nothing") : out;
}

static string _clock(int64_t ms)
{
    auto s = r_time_utils::tp_to_iso_8601(r_time_utils::epoch_millis_to_tp(ms), false);
    return s.size() >= 19 ? s.substr(11, 8) : s;
}

r_detection_pass::r_detection_pass(frame_fn live, frame_fn stored, detect_fn detect, name_fn name,
                                   found_fn on_found, session_fn on_session) :
    _live(std::move(live)),
    _stored(std::move(stored)),
    _detect(std::move(detect)),
    _name(std::move(name)),
    _on_found(std::move(on_found)),
    _on_session(std::move(on_session))
{
}

r_detection_pass::~r_detection_pass() noexcept
{
    stop();
}

void r_detection_pass::start()
{
    _running = true;
    _th = thread(&r_detection_pass::_entry_point, this);
}

void r_detection_pass::stop()
{
    {
        lock_guard<mutex> g(_lock);
        _running = false;
    }
    _cv.notify_all();
    if (_th.joinable())
        _th.join();
}

void r_detection_pass::motion(const string& camera_id, const string& source, bool active)
{
    {
        lock_guard<mutex> g(_lock);
        _planner.motion(camera_id, source, active, _now_ms());
        _kick = true;
    }
    _cv.notify_all();
}

void r_detection_pass::camera_class(const string& camera_id, const string& what)
{
    lock_guard<mutex> g(_lock);
    _planner.camera_class(camera_id, what, _now_ms());
}

vector<r_detection_session> r_detection_pass::results(const string& camera_id, int64_t since_ms) const
{
    lock_guard<mutex> g(_lock);
    vector<r_detection_session> out;
    for (auto& r : _results)
        if (r.camera_id == camera_id && r.start_ms >= since_ms)
            out.push_back(r);
    return out;
}

void r_detection_pass::_entry_point()
{
    while (_running)
    {
        r_nullable<r_look> look;
        vector<r_detection_session> done;
        {
            unique_lock<mutex> g(_lock);
            auto now = _now_ms();
            done = _planner.finished(now);
            for (auto& d : done)
            {
                _results.push_back(d);
                if (_results.size() > MAX_RESULTS)
                    _results.pop_front();
            }
            look = _planner.next_look(now);
            if (look.is_null() && done.empty())
            {
                auto due = _planner.next_due_ms();
                int64_t wait = (due == 0) ? 1000 : std::clamp<int64_t>(due - now, 1, 1000);
                _cv.wait_for(g, milliseconds(wait), [this]() { return !_running || _kick; });
                _kick = false;
                continue;
            }
        }

        for (auto& d : done)
        {
            _log_session(d);
            if (_on_session)
                _on_session(d);
        }

        if (look.is_null())
            continue;

        auto& l = look.value();
        bool got = false;
        int64_t frame_ts = 0;
        vector<r_detection> dets;
        try
        {
            auto frame = _is_live(l.kind) ? _live(l.camera_id, 0) : _stored(l.camera_id, l.ts);
            if (!frame.is_null())
            {
                frame_ts = frame.value().ts;
                bool usable = _is_live(l.kind) ? (_now_ms() - frame_ts <= LIVE_MAX_AGE_MS)
                                            : (l.kind == look_pre_roll || l.ts - frame_ts <= STORED_MAX_GAP_MS);
                if (usable)
                    got = _detect(l.camera_id, frame.value(), dets);
            }
        }
        catch (const exception& e)
        {
            R_LOG_WARNING("detection pass [%s]: %s look failed: %s", _name(l.camera_id).c_str(), look_kind_name(l.kind), e.what());
        }

        if (got && l.kind == look_first)
            _log_first_look(l, dets);

        vector<r_detection> found;
        {
            lock_guard<mutex> g(_lock);
            found = _planner.look_done(l, got, frame_ts, dets, _now_ms());
        }
        if (!found.empty() && _on_found)
            _on_found(l.camera_id, found);
    }
}

void r_detection_pass::_log_first_look(const r_look& look, const vector<r_detection>& dets)
{
    // The first look falls due FIRST_LOOK_MS after motion started.
    double after_s = (double)(_now_ms() - (look.due_ms - r_detection_planner::FIRST_LOOK_MS)) / 1000.0;
    R_LOG_INFO("detection pass [%s]: first look %.1f s after motion: %s",
               _name(look.camera_id).c_str(), after_s, _summary(dets, false).c_str());
}

void r_detection_pass::_log_session(const r_detection_session& s)
{
    string camera_said;
    for (auto& c : s.camera_classes)
        camera_said += (camera_said.empty() ? "" : ", ") + c;

    string found_after;
    if (s.first_found_ms)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "; first found %.1f s after motion", (double)(s.first_found_ms - s.start_ms) / 1000.0);
        found_after = buf;
    }

    R_LOG_INFO("detection pass [%s]: session %s-%s (%lld s, %d looks%s): %s%s%s; camera said: %s",
               _name(s.camera_id).c_str(),
               _clock(s.start_ms).c_str(), _clock(s.end_ms).c_str(),
               (long long)((s.end_ms - s.start_ms) / 1000), s.looks,
               s.failed_looks ? (", " + to_string(s.failed_looks) + " failed").c_str() : "",
               _summary(s.detections, true).c_str(),
               s.dropped_static ? ("; dropped " + to_string(s.dropped_static) + " already there").c_str() : "",
               found_after.c_str(),
               camera_said.empty() ? "nothing" : camera_said.c_str());
}
