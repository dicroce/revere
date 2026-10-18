#include "r_vss/r_tier_controller.h"
#include "r_utils/r_string_utils.h"
#include <algorithm>

using namespace r_vss;
using namespace r_utils;
using namespace std;

static const int64_t KEEP_MS = 10 * 60 * 1000;  // camera spans and YOLO persons kept for checking

void r_tier_controller::set_setting(const string& camera_id, const string& setting)
{
    auto& c = _cameras[camera_id];
    c.forced = (setting == "1") ? 1 : (setting == "2") ? 2 : (setting == "3") ? 3 : 0;
}

void r_tier_controller::subscription(const string& camera_id, bool up, int64_t now_ms)
{
    auto& c = _cameras[camera_id];
    if (!c.known || up != c.up)
    {
        if (up)
            c.up_since = now_ms;
        else
            c.down_since = now_ms;
    }
    c.known = true;
    c.up = up;
}

void r_tier_controller::camera_motion(const string& camera_id, bool active, int64_t now_ms)
{
    auto& c = _cameras[camera_id];
    c.last_motion_ms = now_ms;
    if (active)
    {
        if (c.motion.empty() || c.motion.back().end != 0)
            c.motion.push_back({now_ms, 0});
    }
    else if (!c.motion.empty() && c.motion.back().end == 0)
        c.motion.back().end = now_ms;
}

void r_tier_controller::camera_class(const string& camera_id, const string& what, bool active, int64_t now_ms)
{
    if (what != "person")
        return;
    auto& c = _cameras[camera_id];
    if (active)
    {
        if (c.person.empty() || c.person.back().end != 0)
            c.person.push_back({now_ms, 0});
    }
    else if (!c.person.empty() && c.person.back().end == 0)
        c.person.back().end = now_ms;
}

void r_tier_controller::mog2_event(const string& camera_id, int64_t start_ms, int64_t end_ms)
{
    _cameras[camera_id].pending.push_back({start_ms, end_ms});
}

void r_tier_controller::yolo_detection(const string& camera_id, const string& class_name, int64_t ts)
{
    if (class_name == "person")
        _cameras[camera_id].yolo_person.push_back(ts);
}

bool r_tier_controller::_overlaps(const deque<_span>& spans, int64_t start, int64_t end, int64_t now_ms)
{
    for (auto& s : spans)
        if (s.start <= end && (s.end == 0 ? now_ms : s.end) >= start)
            return true;
    return false;
}

void r_tier_controller::_check_pending(_camera& c, int64_t now_ms)
{
    while (!c.pending.empty() && c.pending.front().second + VERIFY_DELAY_MS <= now_ms)
    {
        auto ev = c.pending.front();
        c.pending.pop_front();

        // The camera couldn't have reported an event that happened while its
        // subscription was down: it says nothing about the camera.
        if (!c.known || !c.up || c.up_since > ev.first)
            continue;

        _check k;
        k.motion = _overlaps(c.motion, ev.first - MATCH_SLACK_MS, ev.second + MATCH_SLACK_MS, now_ms);
        k.person = any_of(c.yolo_person.begin(), c.yolo_person.end(), [&](int64_t ts) {
            return ts >= ev.first - MATCH_SLACK_MS && ts <= ev.second + MATCH_SLACK_MS;
        });
        k.camera_person = k.person && _overlaps(c.person, ev.first - MATCH_SLACK_MS, ev.second + MATCH_SLACK_MS, now_ms);
        c.window.push_back(k);
        while (c.window.size() > WINDOW)
            c.window.pop_front();
    }
}

void r_tier_controller::_verify(_camera& c)
{
    int checked = (int)c.window.size();
    int matched = 0, person = 0, camera_person = 0;
    for (auto& k : c.window)
    {
        matched += k.motion ? 1 : 0;
        person += k.person ? 1 : 0;
        camera_person += k.camera_person ? 1 : 0;
    }
    if (checked < MIN_EVENTS || matched < MATCH_RATE * checked)
        return;
    c.verified_tier = (person >= MIN_PERSON_EVENTS && camera_person >= MATCH_RATE * person) ? motion_tier_2 : motion_tier_3;
}

int r_tier_controller::_decide(_camera& c, int64_t now_ms)
{
    bool down_long = c.known && !c.up && now_ms - c.down_since >= DOWN_MS;

    if (c.forced == motion_tier_1)
    {
        c.reason = "set to Revere's own detection";
        return motion_tier_1;
    }

    if (c.forced != 0)
    {
        if (!c.known)
        {
            c.reason = "set to the camera's events, but it has no event subscription";
            return motion_tier_1;
        }
        if (down_long)
        {
            c.reason = "the camera's events are down; Revere's own detection until they are back";
            return motion_tier_1;
        }
        c.reason = "set to the camera's events";
        return c.forced;
    }

    if (!c.known)
    {
        c.reason = "the camera has no event subscription";
        return motion_tier_1;
    }

    if (c.verified_tier == 0)
    {
        _verify(c);
        if (c.verified_tier != 0)
            c.verified_at = now_ms;
    }

    if (c.verified_tier == 0)
    {
        auto st = status_of(c);
        if (st.checked >= MIN_EVENTS)
            c.reason = r_string_utils::format("the camera reported %d of the last %d motion events Revere saw; check its detection zones and schedule", st.matched, st.checked);
        else
            c.reason = r_string_utils::format("checking the camera's events against Revere's (%d of %d events so far)", st.checked, (int)MIN_EVENTS);
        return motion_tier_1;
    }

    // Verified. Quiet for longer than anything real: check it again.
    if (now_ms - max(c.last_motion_ms, c.verified_at) >= SILENCE_MS)
    {
        c.verified_tier = 0;
        c.window.clear();
        c.reason = "the camera reported no motion for 12 hours; checking it again";
        return motion_tier_1;
    }
    if (down_long)
    {
        c.reason = "the camera's events are down; Revere's own detection until they are back";
        return motion_tier_1;
    }
    if (c.tier == motion_tier_1 && (!c.up || now_ms - c.up_since < STABLE_MS))
    {
        c.reason = "the camera's events match; waiting for its subscription to stay up";
        return motion_tier_1;
    }
    c.reason = (c.verified_tier == motion_tier_2)
        ? "the camera's motion and person events match Revere's"
        : "the camera's motion events match Revere's; Revere finds the objects";
    return c.verified_tier;
}

void r_tier_controller::_prune(_camera& c, int64_t now_ms)
{
    auto old = [&](const _span& s) { return s.end != 0 && now_ms - s.end > KEEP_MS; };
    while (!c.motion.empty() && old(c.motion.front()))
        c.motion.pop_front();
    while (!c.person.empty() && old(c.person.front()))
        c.person.pop_front();
    while (!c.yolo_person.empty() && now_ms - c.yolo_person.front() > KEEP_MS)
        c.yolo_person.pop_front();
}

vector<string> r_tier_controller::tick(int64_t now_ms)
{
    vector<string> changed;
    for (auto& p : _cameras)
    {
        auto& c = p.second;
        _check_pending(c, now_ms);
        int t = _decide(c, now_ms);
        if (t != c.tier)
        {
            c.tier = t;
            changed.push_back(p.first);
        }
        _prune(c, now_ms);
    }
    return changed;
}

int r_tier_controller::tier(const string& camera_id) const
{
    auto it = _cameras.find(camera_id);
    return it == _cameras.end() ? (int)motion_tier_1 : it->second.tier;
}

r_tier_status r_tier_controller::status_of(const _camera& c)
{
    r_tier_status s;
    s.tier = c.tier;
    s.automatic = c.forced == 0;
    s.reason = c.reason;
    s.checked = (int)c.window.size();
    for (auto& k : c.window)
    {
        s.matched += k.motion ? 1 : 0;
        s.person_checked += k.person ? 1 : 0;
        s.person_matched += k.camera_person ? 1 : 0;
    }
    return s;
}

r_tier_status r_tier_controller::status(const string& camera_id) const
{
    auto it = _cameras.find(camera_id);
    return it == _cameras.end() ? r_tier_status() : status_of(it->second);
}

void r_tier_controller::remove(const string& camera_id)
{
    _cameras.erase(camera_id);
}
