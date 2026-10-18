#include "test_r_detection_planner.h"
#include "r_vss/r_detection_planner.h"
#include <functional>
#include <string>
#include <vector>

using namespace r_vss;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_detection_planner);

typedef r_detection_planner P;

static r_detection _det(const string& cls, float x1, float y1, float x2, float y2)
{
    r_detection d;
    d.class_name = cls;
    d.confidence = 0.9f;
    d.x1 = x1; d.y1 = y1; d.x2 = x2; d.y2 = y2;
    return d;
}

// Steps time from `from` to `to` in 100 ms steps, doing every look that falls
// due (the frame is the one wanted, its detections from `dets`), and collects
// finished sessions.
static void _run(P& p, int64_t from, int64_t to, const function<vector<r_detection>(const r_look&)>& dets,
                 vector<r_look>* looks = nullptr, vector<r_detection_session>* finished = nullptr)
{
    for (int64_t t = from; t <= to; t += 100)
    {
        while (true)
        {
            auto l = p.next_look(t);
            if (l.is_null())
                break;
            if (looks)
                looks->push_back(l.value());
            p.look_done(l.value(), true, l.value().ts, dets(l.value()), t);
        }
        auto f = p.finished(t);
        if (finished)
            finished->insert(finished->end(), f.begin(), f.end());
    }
}

static vector<r_detection> _none(const r_look&) { return {}; }

void test_r_detection_planner::test_first_look_then_follow_ups()
{
    P p;
    p.motion("a", "", true, 1000);

    // The pre-roll is due at once: its frame is already stored.
    auto pre = p.next_look(1000);
    RTF_ASSERT(!pre.is_null());
    RTF_ASSERT(pre.value().kind == look_pre_roll);
    RTF_ASSERT_EQUAL(pre.value().ts, (int64_t)(1000 - P::PRE_ROLL_MS));
    p.look_done(pre.value(), true, -2000, {}, 1200);

    RTF_ASSERT(p.next_look(2999).is_null());
    RTF_ASSERT_EQUAL(p.next_due_ms(), (int64_t)3000);

    auto first = p.next_look(3000);
    RTF_ASSERT(!first.is_null());
    RTF_ASSERT(first.value().kind == look_first);
    p.look_done(first.value(), true, 2900, {_det("person", 100, 100, 200, 300)}, 3100);

    // Then the start look.
    auto start = p.next_look(3100);
    RTF_ASSERT(!start.is_null());
    RTF_ASSERT(start.value().kind == look_start);
    RTF_ASSERT_EQUAL(start.value().ts, (int64_t)1000);
    p.look_done(start.value(), true, 900, {}, 3150);
    RTF_ASSERT(p.next_look(3150).is_null());

    // Something was found, so the follow-up is the slow one.
    RTF_ASSERT(p.next_look(3100 + P::FOLLOW_UP_FOUND_MS - 1).is_null());
    auto fu = p.next_look(3100 + P::FOLLOW_UP_FOUND_MS);
    RTF_ASSERT(!fu.is_null());
    RTF_ASSERT(fu.value().kind == look_follow_up);
    p.look_done(fu.value(), true, 15000, {}, 15200);

    // Motion ends: the planned follow-up goes, an end look comes.
    p.motion("a", "", false, 20000);
    RTF_ASSERT(p.next_look(21999).is_null());
    auto end = p.next_look(22000);
    RTF_ASSERT(!end.is_null());
    RTF_ASSERT(end.value().kind == look_end);
    RTF_ASSERT_EQUAL(end.value().ts, (int64_t)20000);
    p.look_done(end.value(), true, 19900, {_det("person", 400, 100, 500, 300)}, 22100);

    RTF_ASSERT(p.finished(20000 + P::CLUSTER_GAP_MS - 1).empty());
    auto f = p.finished(20000 + P::CLUSTER_GAP_MS);
    RTF_ASSERT_EQUAL(f.size(), (size_t)1);
    RTF_ASSERT_EQUAL(f[0].start_ms, (int64_t)1000);
    RTF_ASSERT_EQUAL(f[0].end_ms, (int64_t)20000);
    RTF_ASSERT_EQUAL(f[0].looks, 5);
    RTF_ASSERT_EQUAL(f[0].detections.size(), (size_t)2);
    RTF_ASSERT_EQUAL(f[0].first_found_ms, (int64_t)3100);
    RTF_ASSERT_EQUAL(f[0].first_found.size(), (size_t)1);
    RTF_ASSERT(p.finished(100000).empty());
}

void test_r_detection_planner::test_short_event_uses_last_key_frame()
{
    P p;
    p.motion("a", "", true, 1000);
    p.motion("a", "", false, 2000);

    vector<r_look> looks;
    vector<r_detection_session> f;
    _run(p, 2000, 40000, [](const r_look& l) {
        return l.kind == look_short ? vector<r_detection>{_det("person", 0, 0, 50, 100)} : vector<r_detection>{};
    }, &looks, &f);

    RTF_ASSERT_EQUAL(looks.size(), (size_t)3);
    RTF_ASSERT(looks[0].kind == look_pre_roll);
    RTF_ASSERT(looks[1].kind == look_start);
    RTF_ASSERT(looks[2].kind == look_short);
    RTF_ASSERT_EQUAL(looks[2].ts, (int64_t)2000);
    RTF_ASSERT_EQUAL(f.size(), (size_t)1);
    RTF_ASSERT_EQUAL(f[0].detections.size(), (size_t)1);
    RTF_ASSERT_EQUAL(f[0].first_found.size(), (size_t)1);
}

void test_r_detection_planner::test_events_cluster_into_one_session()
{
    P p;
    vector<r_look> looks;
    vector<r_detection_session> f;
    auto person = [](const r_look& l) {
        float x = (float)(l.ts % 500);
        return l.kind == look_pre_roll ? vector<r_detection>{} : vector<r_detection>{_det("person", x, 0, x + 50, 100)};
    };

    p.motion("a", "", true, 1000);
    _run(p, 1000, 5000, person, &looks, &f);
    p.motion("a", "", false, 5000);
    _run(p, 5100, 10000, person, &looks, &f);
    p.motion("a", "", true, 10000);  // 5 s later: the same session
    _run(p, 10100, 15000, person, &looks, &f);
    p.motion("a", "", false, 15000);
    _run(p, 15100, 50000, person, &looks, &f);

    RTF_ASSERT_EQUAL(f.size(), (size_t)1);
    RTF_ASSERT_EQUAL(f[0].start_ms, (int64_t)1000);
    RTF_ASSERT_EQUAL(f[0].end_ms, (int64_t)15000);

    int first = 0, pre = 0, start = 0;
    for (auto& l : looks)
    {
        if (l.kind == look_first) ++first;
        if (l.kind == look_pre_roll) ++pre;
        if (l.kind == look_start) ++start;
    }
    RTF_ASSERT_EQUAL(first, 1);
    RTF_ASSERT_EQUAL(pre, 1);
    RTF_ASSERT_EQUAL(start, 2);  // one per event
}

void test_r_detection_planner::test_far_apart_events_are_separate()
{
    P p;
    vector<r_detection_session> f;
    p.motion("a", "", true, 1000);
    p.motion("a", "", false, 2000);
    _run(p, 2000, 39000, _none, nullptr, &f);
    p.motion("a", "", true, 40000);
    p.motion("a", "", false, 41000);
    _run(p, 41000, 80000, _none, nullptr, &f);

    RTF_ASSERT_EQUAL(f.size(), (size_t)2);
    RTF_ASSERT_EQUAL(f[0].start_ms, (int64_t)1000);
    RTF_ASSERT_EQUAL(f[1].start_ms, (int64_t)40000);
}

void test_r_detection_planner::test_pre_roll_drops_parked_objects()
{
    P p;
    vector<r_detection_session> f;
    auto parked = _det("car", 300, 300, 500, 400);
    p.motion("a", "", true, 1000);
    _run(p, 1000, 4000, [&](const r_look& l) {
        if (l.kind == look_pre_roll)
            return vector<r_detection>{parked};
        return vector<r_detection>{parked, _det("person", 10, 10, 60, 160)};
    }, nullptr, &f);
    p.motion("a", "", false, 4000);
    _run(p, 4100, 40000, [&](const r_look&) {
        return vector<r_detection>{_det("car", 302, 301, 501, 400), _det("person", 200, 10, 250, 160)};
    }, nullptr, &f);

    RTF_ASSERT_EQUAL(f.size(), (size_t)1);
    RTF_ASSERT_EQUAL(f[0].detections.size(), (size_t)3);   // first, start and end looks
    for (auto& d : f[0].detections)
        RTF_ASSERT_EQUAL(d.class_name, string("person"));
    RTF_ASSERT_EQUAL(f[0].dropped_static, 3);
    // The pre-roll ran before the first look, so the parked car isn't news.
    RTF_ASSERT_EQUAL(f[0].first_found.size(), (size_t)1);
    RTF_ASSERT_EQUAL(f[0].first_found[0].class_name, string("person"));
}

void test_r_detection_planner::test_first_and_last_frame_static_check()
{
    P p;
    vector<r_detection_session> f;
    auto bin = _det("suitcase", 100, 400, 150, 480);  // missed by the pre-roll
    p.motion("a", "", true, 1000);
    _run(p, 1000, 20000, [&](const r_look& l) {
        if (l.kind == look_pre_roll)
            return vector<r_detection>{};
        if (l.kind == look_follow_up)
            return vector<r_detection>{bin, _det("person", 300, 100, 350, 250)};
        return vector<r_detection>{bin};
    }, nullptr, &f);
    p.motion("a", "", false, 20000);
    _run(p, 20100, 55000, [&](const r_look&) { return vector<r_detection>{bin}; }, nullptr, &f);

    RTF_ASSERT_EQUAL(f.size(), (size_t)1);
    RTF_ASSERT_EQUAL(f[0].detections.size(), (size_t)1);
    RTF_ASSERT_EQUAL(f[0].detections[0].class_name, string("person"));
    RTF_ASSERT_EQUAL(f[0].dropped_static, 4);   // the bin in the first, start, follow-up and end looks
}

void test_r_detection_planner::test_failed_stored_look_retries_once()
{
    P p;
    p.motion("a", "", true, 1000);
    p.motion("a", "", false, 2000);

    int middles = 0;
    vector<r_detection_session> f;
    for (int64_t t = 2000; t <= 45000; t += 100)
    {
        while (true)
        {
            auto l = p.next_look(t);
            if (l.is_null())
                break;
            bool ok = l.value().kind != look_short;
            if (!ok)
            {
                ++middles;
                RTF_ASSERT_EQUAL(l.value().attempt, middles - 1);
            }
            p.look_done(l.value(), ok, l.value().ts, {}, t);
        }
        auto fin = p.finished(t);
        f.insert(f.end(), fin.begin(), fin.end());
    }

    RTF_ASSERT_EQUAL(middles, (int)P::MAX_ATTEMPTS);
    RTF_ASSERT_EQUAL(f.size(), (size_t)1);
    RTF_ASSERT_EQUAL(f[0].failed_looks, (int)P::MAX_ATTEMPTS);
    RTF_ASSERT_EQUAL(f[0].looks, 2);  // the start look and the pre-roll
}

// Two cameras' stored looks all due at once take turns, rather than one
// camera's going first because they fell due first.
void test_r_detection_planner::test_fair_between_cameras()
{
    P p;
    p.motion("a", "", true, 0);
    p.motion("a", "", false, 100);    // pre-roll 0, start 2000, short 2100
    p.motion("b", "", true, 1000);
    p.motion("b", "", false, 1100);   // pre-roll 1000, start 3000, short 3100

    vector<string> order;
    while (true)
    {
        auto l = p.next_look(4000);
        if (l.is_null())
            break;
        order.push_back(l.value().camera_id + ":" + look_kind_name(l.value().kind));
        p.look_done(l.value(), true, l.value().ts, {}, 4000);
    }

    RTF_ASSERT_EQUAL(order.size(), (size_t)6);
    RTF_ASSERT_EQUAL(order[0], string("a:start"));
    RTF_ASSERT_EQUAL(order[1], string("b:start"));
    RTF_ASSERT_EQUAL(order[2], string("a:pre-roll"));
    RTF_ASSERT_EQUAL(order[3], string("b:pre-roll"));
    RTF_ASSERT_EQUAL(order[4], string("a:short"));
    RTF_ASSERT_EQUAL(order[5], string("b:short"));

    // A first look beats a follow-up that fell due earlier.
    P q;
    q.motion("a", "", true, 0);
    auto a1 = q.next_look(2000);
    q.look_done(a1.value(), true, 2000, {}, 2000);     // a's follow-up due 6000
    for (auto more = q.next_look(2001); !more.is_null(); more = q.next_look(2001))
        q.look_done(more.value(), true, 0, {}, 2001);         // a's start look and pre-roll
    q.motion("b", "", true, 5000);                      // b's first look due 7000
    auto l = q.next_look(7000);
    RTF_ASSERT_EQUAL(l.value().camera_id, string("b"));
    RTF_ASSERT(l.value().kind == look_first);
}

void test_r_detection_planner::test_early_camera_class()
{
    P p;
    p.camera_class("a", "animal", -5000);    // too early
    p.camera_class("a", "vehicle", 600);     // Porch: the class just before the motion
    p.motion("a", "", true, 1000);
    p.camera_class("a", "person", 3000);
    p.camera_class("a", "person", 3500);
    p.motion("a", "", false, 6000);

    vector<r_detection_session> f;
    _run(p, 6000, 40000, _none, nullptr, &f);
    RTF_ASSERT_EQUAL(f.size(), (size_t)1);
    RTF_ASSERT_EQUAL(f[0].camera_classes.size(), (size_t)2);
    RTF_ASSERT_EQUAL(f[0].camera_classes[0], string("vehicle"));
    RTF_ASSERT_EQUAL(f[0].camera_classes[1], string("person"));
}

void test_r_detection_planner::test_sources()
{
    P p;
    vector<r_look> looks;
    p.motion("a", "src=1", true, 1000);
    p.motion("a", "src=2", true, 1500);
    p.motion("a", "src=1", false, 3500);
    p.motion("a", "src=3", false, 3600);  // never started: ignored
    _run(p, 1000, 9000, _none, &looks);

    bool end_look = false;
    for (auto& l : looks)
        end_look = end_look || l.kind == look_end;
    RTF_ASSERT(!end_look);  // still moving on src=2

    p.motion("a", "src=2", false, 9000);
    vector<r_detection_session> f;
    _run(p, 9100, 45000, _none, &looks, &f);
    RTF_ASSERT_EQUAL(f.size(), (size_t)1);
    RTF_ASSERT_EQUAL(f[0].end_ms, (int64_t)9000);
    RTF_ASSERT(looks.back().kind == look_end);
}

void test_r_detection_planner::test_look_cap()
{
    P p;
    vector<r_detection_session> f;
    p.motion("a", "", true, 0);
    _run(p, 0, 600000, _none, nullptr, &f);   // ten minutes of motion, nothing found
    p.motion("a", "", false, 600000);
    _run(p, 600100, 640000, _none, nullptr, &f);

    RTF_ASSERT_EQUAL(f.size(), (size_t)1);
    // The end look is allowed past the cap.
    RTF_ASSERT_EQUAL(f[0].looks, (int)P::MAX_LOOKS + 1);
}
