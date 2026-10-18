#include "r_vss/r_detection_planner.h"
#include <algorithm>

using namespace r_vss;
using namespace r_utils;
using namespace std;

const char* r_vss::look_kind_name(r_look_kind kind)
{
    switch (kind)
    {
        case look_first: return "first";
        case look_follow_up: return "follow-up";
        case look_pre_roll: return "pre-roll";
        case look_short: return "short";
        case look_end: return "end";
    }
    return "unknown";
}

static bool _live(r_look_kind kind)
{
    return kind == look_first || kind == look_follow_up;
}

static int _rank(r_look_kind kind)
{
    // First looks decide how quickly an alert can go out; stored looks only
    // refine a session's result.
    return kind == look_first ? 0 : (kind == look_follow_up ? 1 : 2);
}

static void _add_unique(vector<string>& v, const string& s)
{
    if (find(v.begin(), v.end(), s) == v.end())
        v.push_back(s);
}

bool r_detection_planner::same_object(const r_detection& a, const r_detection& b)
{
    if (a.class_name != b.class_name)
        return false;
    float ix1 = max(a.x1, b.x1), iy1 = max(a.y1, b.y1);
    float ix2 = min(a.x2, b.x2), iy2 = min(a.y2, b.y2);
    if (ix1 >= ix2 || iy1 >= iy2)
        return false;
    float inter = (ix2 - ix1) * (iy2 - iy1);
    float uni = (a.x2 - a.x1) * (a.y2 - a.y1) + (b.x2 - b.x1) * (b.y2 - b.y1) - inter;
    return uni > 0.0f && inter / uni >= 0.85f;
}

void r_detection_planner::_plan_live(_session& s, int64_t due_ms)
{
    if (s.live_in_flight || s.planned >= MAX_LOOKS)
        return;
    if (s.next_live_ms == 0)
        ++s.planned;
    s.next_live_ms = due_ms;
}

void r_detection_planner::_plan_stored(_session& s, r_look_kind kind, int64_t ts, int64_t due_ms)
{
    // One end look at a time: a later end replaces the planned one.
    if (kind == look_end)
    {
        for (auto& l : s.stored)
        {
            if (l.kind == look_end)
            {
                l.ts = ts;
                l.due_ms = due_ms;
                l.attempt = 0;
                return;
            }
        }
    }
    if (s.planned >= MAX_LOOKS && kind != look_end)
        return;
    ++s.planned;
    r_look l;
    l.camera_id = s.result.camera_id;
    l.kind = kind;
    l.ts = ts;
    l.due_ms = due_ms;
    s.stored.push_back(l);
}

void r_detection_planner::motion(const string& camera_id, const string& source, bool active, int64_t now_ms)
{
    auto it = _sessions.find(camera_id);

    // A session past its cluster gap is over even if nobody asked yet.
    if (it != _sessions.end() && active && _done(it->second, now_ms))
    {
        _finished.push_back(_finish(it->second));
        _sessions.erase(it);
        it = _sessions.end();
    }

    if (active)
    {
        if (it == _sessions.end())
        {
            _session s;
            s.result.camera_id = camera_id;
            s.result.start_ms = now_ms;
            s.event_start_ms = now_ms;
            s.active_sources.insert(source);

            auto eit = _early_classes.find(camera_id);
            if (eit != _early_classes.end())
            {
                for (auto& c : eit->second)
                    if (now_ms - c.second <= EARLY_CLASS_MS)
                        _add_unique(s.result.camera_classes, c.first);
                _early_classes.erase(eit);
            }

            _plan_live(s, now_ms + FIRST_LOOK_MS);
            // After the first look: it only refines the result.
            _plan_stored(s, look_pre_roll, now_ms - PRE_ROLL_MS, now_ms + FIRST_LOOK_MS + 1);
            _sessions[camera_id] = s;
            return;
        }

        auto& s = it->second;
        bool was_active = _active(s);
        s.active_sources.insert(source);
        if (!was_active)
        {
            // Motion again within the cluster gap: the same session.
            s.event_start_ms = now_ms;
            s.result.end_ms = 0;
            _plan_live(s, now_ms + (s.found ? FOLLOW_UP_FOUND_MS : FIRST_LOOK_MS));
        }
        return;
    }

    if (it == _sessions.end())
        return;
    auto& s = it->second;
    if (s.active_sources.erase(source) == 0 || _active(s))
        return;

    s.last_end_ms = now_ms;
    s.result.end_ms = now_ms;
    s.next_live_ms = 0;
    if (s.live_ok)
        _plan_stored(s, look_end, now_ms, now_ms + STORAGE_DELAY_MS);
    else
    {
        // It ended before a live look saw it: look at its last key frame instead.
        _plan_stored(s, look_short, now_ms, now_ms + STORAGE_DELAY_MS);
    }
}

void r_detection_planner::camera_class(const string& camera_id, const string& what, int64_t now_ms)
{
    auto it = _sessions.find(camera_id);
    if (it != _sessions.end() && !_done(it->second, now_ms))
    {
        _add_unique(it->second.result.camera_classes, what);
        return;
    }
    auto& early = _early_classes[camera_id];
    early.push_back({what, now_ms});
    while (!early.empty() && now_ms - early.front().second > EARLY_CLASS_MS)
        early.pop_front();
}

r_nullable<r_look> r_detection_planner::next_look(int64_t now_ms)
{
    r_nullable<r_look> best;
    int64_t best_served = 0;
    auto better = [&](const r_look& l) {
        if (best.is_null())
            return true;
        int r = _rank(l.kind), br = _rank(best.value().kind);
        if (r != br)
            return r < br;
        auto served = _last_served.count(l.camera_id) ? _last_served[l.camera_id] : 0;
        if (served != best_served)
            return served < best_served;
        return l.due_ms < best.value().due_ms;
    };

    for (auto& p : _sessions)
    {
        auto& s = p.second;
        if (s.next_live_ms != 0 && !s.live_in_flight && s.next_live_ms <= now_ms)
        {
            r_look l;
            l.camera_id = p.first;
            l.kind = s.live_ok ? look_follow_up : look_first;
            l.ts = s.next_live_ms;
            l.due_ms = s.next_live_ms;
            if (better(l))
            {
                best.set_value(l);
                best_served = _last_served.count(p.first) ? _last_served[p.first] : 0;
            }
        }
        for (auto& l : s.stored)
        {
            if (l.due_ms <= now_ms && better(l))
            {
                best.set_value(l);
                best_served = _last_served.count(p.first) ? _last_served[p.first] : 0;
            }
        }
    }

    if (best.is_null())
        return best;

    auto& l = best.value();
    auto& s = _sessions[l.camera_id];
    if (_live(l.kind))
    {
        s.live_in_flight = true;
        s.next_live_ms = 0;
    }
    else
    {
        for (auto it = s.stored.begin(); it != s.stored.end(); ++it)
        {
            if (it->kind == l.kind && it->ts == l.ts)
            {
                s.stored.erase(it);
                break;
            }
        }
    }
    ++s.in_flight;
    _last_served[l.camera_id] = now_ms;
    return best;
}

int64_t r_detection_planner::next_due_ms() const
{
    int64_t due = 0;
    auto take = [&](int64_t t) { if (t != 0 && (due == 0 || t < due)) due = t; };
    for (auto& p : _sessions)
    {
        auto& s = p.second;
        if (!s.live_in_flight)
            take(s.next_live_ms);
        for (auto& l : s.stored)
            take(l.due_ms);
        if (!_active(s) && s.in_flight == 0 && s.stored.empty() && s.next_live_ms == 0)
            take(s.last_end_ms + CLUSTER_GAP_MS);
    }
    return due;
}

void r_detection_planner::look_done(const r_look& look, bool got_frame, int64_t frame_ts, const vector<r_detection>& detections, int64_t now_ms)
{
    auto it = _sessions.find(look.camera_id);
    if (it == _sessions.end())
        return;
    auto& s = it->second;
    if (s.in_flight > 0)
        --s.in_flight;
    if (_live(look.kind))
        s.live_in_flight = false;

    if (!got_frame)
    {
        ++s.result.failed_looks;
        if (!_live(look.kind))
        {
            if (look.attempt + 1 < MAX_ATTEMPTS)
            {
                r_look retry = look;
                ++retry.attempt;
                retry.due_ms = now_ms + RETRY_MS;
                s.stored.push_back(retry);
            }
        }
        else if (_active(s))
            _plan_live(s, now_ms + FOLLOW_UP_MS);
        else if (!s.live_ok)
        {
            // The live look failed and the event is over: the stored frame is the fallback.
            _plan_stored(s, look_short, s.last_end_ms, now_ms);
        }
        return;
    }

    ++s.result.looks;

    // Found means something the pre-roll didn't already show.
    auto is_new = [&](const r_detection& d) {
        for (auto& p : s.pre_roll)
            if (same_object(p, d))
                return false;
        return true;
    };

    if (look.kind == look_pre_roll)
    {
        s.pre_roll = detections;
        s.found = any_of(s.seen.begin(), s.seen.end(), is_new);
        return;
    }

    if (s.result.first_look_ms == 0)
    {
        s.result.first_look_ms = now_ms;
        s.result.first_look = detections;
    }
    if (_live(look.kind))
        s.live_ok = true;
    if (any_of(detections.begin(), detections.end(), is_new))
        s.found = true;
    s.seen.insert(s.seen.end(), detections.begin(), detections.end());

    if (s.first_frame_ts < 0 || frame_ts < s.first_frame_ts)
    {
        s.first_frame_ts = frame_ts;
        s.first_frame = detections;
    }
    if (frame_ts > s.last_frame_ts)
    {
        s.last_frame_ts = frame_ts;
        s.last_frame = detections;
    }

    if (_live(look.kind) && _active(s))
        _plan_live(s, now_ms + (s.found ? FOLLOW_UP_FOUND_MS : FOLLOW_UP_MS));
}

bool r_detection_planner::_done(const _session& s, int64_t now_ms) const
{
    return !_active(s) && s.in_flight == 0 && s.stored.empty() && s.next_live_ms == 0 &&
           now_ms - s.last_end_ms >= CLUSTER_GAP_MS;
}

vector<r_detection_session> r_detection_planner::finished(int64_t now_ms)
{
    vector<r_detection_session> out;
    out.swap(_finished);
    for (auto it = _sessions.begin(); it != _sessions.end();)
    {
        if (_done(it->second, now_ms))
        {
            out.push_back(_finish(it->second));
            it = _sessions.erase(it);
        }
        else
            ++it;
    }
    return out;
}

r_detection_session r_detection_planner::_finish(_session& s)
{
    // In the same place at the session's first and last frames: parked, not moving.
    vector<r_detection> anchors;
    if (s.first_frame_ts >= 0 && s.last_frame_ts > s.first_frame_ts)
        for (auto& a : s.first_frame)
            for (auto& b : s.last_frame)
                if (same_object(a, b))
                    anchors.push_back(a);

    auto already_there = [&](const r_detection& d) {
        for (auto& p : s.pre_roll)
            if (same_object(p, d))
                return true;
        for (auto& a : anchors)
            if (same_object(a, d))
                return true;
        return false;
    };

    auto result = s.result;
    for (auto& d : s.seen)
    {
        if (already_there(d))
            ++result.dropped_static;
        else
            result.detections.push_back(d);
    }
    return result;
}
