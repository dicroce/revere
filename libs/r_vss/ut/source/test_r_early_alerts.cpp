
#include "test_r_early_alerts.h"
#include "r_vss/r_early_alerts.h"
#include <cmath>
#include <string>
#include <vector>

using namespace r_vss;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_early_alerts);

typedef r_early_alerts E;

static const int64_t MIN = 60 * 1000;
static const int64_t HOUR = 60 * MIN;
static const int64_t T0 = 1760000000000LL;

static r_box box(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
static r_box shift(const r_box& b, float dx, float dy) { return {b.x1 + dx, b.y1 + dy, b.x2 + dx, b.y2 + dy}; }

// The parked car and the "person" that is leaves, where YOLO puts them.
static const r_box PARKED_CAR = box(400, 300, 160, 90);
static const r_box LEAVES = box(60, 120, 40, 90);

// One event on camera "c" at ts: its start and end frames saw these.
static bool _event(E& e, int64_t ts, const vector<E::r_seen>& start, const vector<E::r_seen>& end)
{
    e.event_started("c");
    return e.event_ended("c", ts, start, end);
}

void test_r_early_alerts::test_box_iou()
{
    RTF_ASSERT(fabs(box_iou(PARKED_CAR, PARKED_CAR) - 1.0f) < 1e-6);
    RTF_ASSERT(box_iou(PARKED_CAR, LEAVES) == 0.0f);
    // Half overlapping along x: 1/3.
    RTF_ASSERT(fabs(box_iou(box(0, 0, 10, 10), box(5, 0, 10, 10)) - 1.0f / 3) < 1e-6);
    RTF_ASSERT(box_iou(box(0, 0, 0, 0), box(0, 0, 0, 0)) == 0.0f);
    RTF_ASSERT(alert_group_of("person") == "person");
    RTF_ASSERT(alert_group_of("truck") == "vehicle");
    RTF_ASSERT(alert_group_of("dog") == "animal");
    RTF_ASSERT(alert_group_of("pottedplant").empty());
}

void test_r_early_alerts::test_floor_and_once_per_class()
{
    E e;
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.59f, LEAVES, T0).verdict, (int)E::early_below_floor);
    RTF_ASSERT_EQUAL((int)e.consider("c", "pottedplant", 0.95f, LEAVES, T0).verdict, (int)E::early_not_alerting);
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.60f, LEAVES, T0 + 1000).verdict, (int)E::early_sent);
    // Once per class per event; another class goes on its own.
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.95f, LEAVES, T0 + 2000).verdict, (int)E::early_already_sent);
    RTF_ASSERT_EQUAL((int)e.consider("c", "car", 0.8f, PARKED_CAR, T0 + 2000).verdict, (int)E::early_sent);
    RTF_ASSERT_EQUAL((int)e.consider("c", "truck", 0.8f, PARKED_CAR, T0 + 2000).verdict, (int)E::early_sent);
    // Another camera is its own event.
    e.event_started("d");
    RTF_ASSERT_EQUAL((int)e.consider("d", "person", 0.7f, LEAVES, T0).verdict, (int)E::early_sent);

    // What went early, by class and frame time, for the end of the event.
    RTF_ASSERT(e.sent_early("c", "person", T0 + 1000));
    RTF_ASSERT(!e.sent_early("c", "person", T0 + 2000));
    RTF_ASSERT(!e.sent_early("c", "person", T0));
    RTF_ASSERT(e.sent_early("c", "car", T0 + 2000));

    // The next event sends again.
    e.event_ended("c", T0 + 5000, {}, {});
    RTF_ASSERT(!e.sent_early("c", "person", T0 + 1000));
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.7f, LEAVES, T0 + 9000).verdict, (int)E::early_sent);
}

void test_r_early_alerts::test_look_interval()
{
    E e;
    RTF_ASSERT_EQUAL(e.look_interval_ms("c"), (int64_t)E::FAST_LOOK_MS);
    e.event_started("c");
    e.consider("c", "person", 0.4f, LEAVES, T0);
    e.consider("c", "pottedplant", 0.9f, LEAVES, T0);
    RTF_ASSERT_EQUAL(e.look_interval_ms("c"), (int64_t)E::FAST_LOOK_MS);
    e.consider("c", "person", 0.9f, LEAVES, T0);
    RTF_ASSERT_EQUAL(e.look_interval_ms("c"), (int64_t)E::SLOW_LOOK_MS);
    RTF_ASSERT_EQUAL(e.look_interval_ms("d"), (int64_t)E::FAST_LOOK_MS);
    e.event_started("c");
    RTF_ASSERT_EQUAL(e.look_interval_ms("c"), (int64_t)E::FAST_LOOK_MS);
}

void test_r_early_alerts::test_parked_car_anchor()
{
    E e;
    // A moving shadow beside a parked car: in both endpoint frames, still.
    RTF_ASSERT(_event(e, T0, {{"car", PARKED_CAR}}, {{"car", shift(PARKED_CAR, 2, 1)}}));
    RTF_ASSERT_EQUAL(e.anchor_count("c", T0), (size_t)1);

    // Next time, YOLO's parked car box (a little off) isn't sent early...
    e.event_started("c");
    auto d = e.consider("c", "car", 0.92f, shift(PARKED_CAR, -6, 4), T0 + HOUR);
    RTF_ASSERT_EQUAL((int)d.verdict, (int)E::early_anchored);
    RTF_ASSERT(d.strong_anchor);
    RTF_ASSERT(d.anchor_iou > 0.8f);
    // ...but a car driving past elsewhere is, and so is a person by the parked car.
    RTF_ASSERT_EQUAL((int)e.consider("c", "car", 0.92f, box(20, 330, 150, 80), T0 + HOUR).verdict, (int)E::early_sent);
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.8f, box(380, 260, 40, 110), T0 + HOUR).verdict, (int)E::early_sent);
    // A person on the car's own box isn't held by the car's anchor.
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.8f, PARKED_CAR, T0 + HOUR).verdict, (int)E::early_sent);
    // A car on half of the parked car's spot (pulling in beside it) isn't held.
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "car", 0.9f, shift(PARKED_CAR, 90, 0), T0 + HOUR).verdict, (int)E::early_sent);
}

void test_r_early_alerts::test_jittery_leaf()
{
    E e;
    // Leaves YOLO calls a person, jumping around between the start and end
    // frames (IoU ~0.6): a sighting each event, not yet an anchor.
    auto jitter = shift(LEAVES, 6, -7);
    RTF_ASSERT(box_iou(LEAVES, jitter) > E::SIGHTING_IOU);
    RTF_ASSERT(box_iou(LEAVES, jitter) < E::STATIC_IOU);
    _event(e, T0, {{"person", LEAVES}}, {{"person", jitter}});
    RTF_ASSERT_EQUAL(e.anchor_count("c", T0), (size_t)0);

    // So the next events still send it early (and the log says so)...
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.66f, shift(LEAVES, -4, 5), T0 + 10 * MIN).verdict, (int)E::early_sent);
    e.event_ended("c", T0 + 10 * MIN + 30000, {{"person", shift(LEAVES, -4, 5)}}, {{"person", shift(LEAVES, 5, 3)}});
    RTF_ASSERT_EQUAL(e.anchor_count("c", T0 + 11 * MIN), (size_t)0);
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.66f, LEAVES, T0 + 20 * MIN).verdict, (int)E::early_sent);
    // ...until the third in two hours makes it a (person) anchor.
    e.event_ended("c", T0 + 20 * MIN + 30000, {{"person", LEAVES}}, {{"person", jitter}});
    RTF_ASSERT_EQUAL(e.anchor_count("c", T0 + 21 * MIN), (size_t)1);

    // Then it's held, wherever it jitters to (IoU >= 0.5).
    e.event_started("c");
    auto d = e.consider("c", "person", 0.71f, shift(LEAVES, 3, -3), T0 + 30 * MIN);
    RTF_ASSERT_EQUAL((int)d.verdict, (int)E::early_anchored);
    RTF_ASSERT(!d.strong_anchor);
    // At 45% the floor holds it anyway.
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.45f, LEAVES, T0 + 30 * MIN).verdict, (int)E::early_below_floor);
    // A person walking up the path, away from the leaves, is sent.
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.83f, box(300, 200, 50, 140), T0 + 30 * MIN).verdict, (int)E::early_sent);

    // Two hours without the leaves: the person anchor has gone.
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.71f, LEAVES, T0 + 20 * MIN + 30000 + E::PERSON_WEAK_TTL_MS + 1).verdict, (int)E::early_sent);

    // A vehicle needs only two sightings and keeps a day.
    E v;
    auto bin = box(500, 400, 60, 70);
    _event(v, T0, {{"truck", bin}}, {{"truck", shift(bin, 8, 6)}});
    RTF_ASSERT_EQUAL(v.anchor_count("c", T0), (size_t)0);
    _event(v, T0 + 10 * MIN, {{"truck", bin}}, {{"truck", shift(bin, -8, 5)}});
    RTF_ASSERT_EQUAL(v.anchor_count("c", T0 + 10 * MIN + E::PERSON_WEAK_TTL_MS + 1), (size_t)1);
}

void test_r_early_alerts::test_doorstep_visitors()
{
    // Visitors standing on the doorstep when the event started and ended,
    // twice in a day, a little apart: no anchor, the next visitor is sent.
    E e;
    auto step = box(260, 160, 60, 170);
    _event(e, T0, {{"person", step}}, {{"person", shift(step, 10, 4)}});
    _event(e, T0 + 5 * HOUR, {{"person", shift(step, -6, 0)}}, {{"person", shift(step, 4, 8)}});
    RTF_ASSERT_EQUAL(e.anchor_count("c", T0 + 5 * HOUR), (size_t)0);
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.8f, step, T0 + 6 * HOUR).verdict, (int)E::early_sent);

    // Twice within the hour: still two sightings, not three.
    E f;
    _event(f, T0, {{"person", step}}, {{"person", shift(step, 10, 4)}});
    _event(f, T0 + 40 * MIN, {{"person", step}}, {{"person", shift(step, 4, 8)}});
    f.event_started("c");
    RTF_ASSERT_EQUAL((int)f.consider("c", "person", 0.8f, step, T0 + 50 * MIN).verdict, (int)E::early_sent);

    // Three in two hours do anchor it, but only for two hours after the last.
    _event(f, T0 + 80 * MIN, {{"person", step}}, {{"person", shift(step, 4, 8)}});
    f.event_started("c");
    RTF_ASSERT_EQUAL((int)f.consider("c", "person", 0.8f, step, T0 + 90 * MIN).verdict, (int)E::early_anchored);
    f.event_started("c");
    RTF_ASSERT_EQUAL((int)f.consider("c", "person", 0.8f, step, T0 + 80 * MIN + E::PERSON_WEAK_TTL_MS + 1).verdict, (int)E::early_sent);

    // A person who stood stock still through the start and end frames (the
    // end-of-event analysis calls that static too) is a strong anchor, kept
    // for the day like any other.
    E h;
    _event(h, T0, {{"person", step}}, {{"person", shift(step, 1, 1)}});
    h.event_started("c");
    RTF_ASSERT_EQUAL((int)h.consider("c", "person", 0.8f, step, T0 + 5 * HOUR).verdict, (int)E::early_anchored);

    // Sightings more than two hours apart don't add up.
    E g;
    for(int i = 0; i < 5; ++i)
        _event(g, T0 + i * 3 * HOUR, {{"person", step}}, {{"person", shift(step, 10, 4)}});
    RTF_ASSERT_EQUAL(g.anchor_count("c", T0 + 12 * HOUR), (size_t)0);
}

void test_r_early_alerts::test_passer_by_is_no_anchor()
{
    E e;
    // In the start frame only, or the end frame only, or somewhere else at
    // the end (they walked on): never an anchor, however often.
    auto door = box(250, 150, 60, 170);
    for(int i = 0; i < 10; ++i)
    {
        _event(e, T0 + i * MIN, {{"person", door}}, {});
        _event(e, T0 + i * MIN + 1, {}, {{"person", door}});
        _event(e, T0 + i * MIN + 2, {{"person", door}}, {{"person", shift(door, 120, 0)}});
        // Same place, different class at the end: no.
        _event(e, T0 + i * MIN + 3, {{"person", door}}, {{"dog", door}});
    }
    RTF_ASSERT_EQUAL(e.anchor_count("c", T0 + 10 * MIN), (size_t)0);
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.8f, door, T0 + 11 * MIN).verdict, (int)E::early_sent);
}

void test_r_early_alerts::test_anchor_lapses()
{
    E e;
    _event(e, T0, {{"car", PARKED_CAR}}, {{"car", PARKED_CAR}});
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "car", 0.9f, PARKED_CAR, T0 + E::ANCHOR_TTL_MS).verdict, (int)E::early_anchored);
    // A day without seeing it: the parked car has gone; a car there is sent.
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "car", 0.9f, PARKED_CAR, T0 + E::ANCHOR_TTL_MS + 1).verdict, (int)E::early_sent);
    RTF_ASSERT_EQUAL(e.anchor_count("c", T0 + E::ANCHOR_TTL_MS + 1), (size_t)0);

    // Seen again within the day, it stays.
    E k;
    for(int64_t t = T0; t < T0 + 3 * E::ANCHOR_TTL_MS; t += 12 * HOUR)
        _event(k, t, {{"car", PARKED_CAR}}, {{"car", PARKED_CAR}});
    RTF_ASSERT_EQUAL(k.anchor_count("c", T0 + 3 * E::ANCHOR_TTL_MS), (size_t)1);

    // Two sightings more than a day apart are lapsed, not an anchor.
    E s;
    auto jitter = shift(LEAVES, 6, -7);
    _event(s, T0, {{"person", LEAVES}}, {{"person", jitter}});
    _event(s, T0 + E::ANCHOR_TTL_MS + 1, {{"person", LEAVES}}, {{"person", jitter}});
    RTF_ASSERT_EQUAL(s.anchor_count("c", T0 + E::ANCHOR_TTL_MS + 1), (size_t)0);
}

void test_r_early_alerts::test_anchor_cap()
{
    E e;
    // 40 parked cars in a row of separate spots, the newest last.
    for(int i = 0; i < 40; ++i)
    {
        auto b = box((float)(i % 8) * 80, (float)(i / 8) * 120, 60, 50);
        _event(e, T0 + i * MIN, {{"car", b}}, {{"car", b}});
    }
    RTF_ASSERT_EQUAL(e.anchor_count("c", T0 + 40 * MIN), (size_t)E::MAX_ANCHORS);
    // The oldest went; the newest stayed.
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "car", 0.9f, box(0, 0, 60, 50), T0 + 41 * MIN).verdict, (int)E::early_sent);
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "car", 0.9f, box(7 * 80, 4 * 120, 60, 50), T0 + 41 * MIN).verdict, (int)E::early_anchored);
}

void test_r_early_alerts::test_json_round_trip()
{
    E e;
    _event(e, T0, {{"car", PARKED_CAR}}, {{"car", PARKED_CAR}});
    auto jitter = shift(LEAVES, 6, -7);
    _event(e, T0 + MIN, {{"person", LEAVES}}, {{"person", jitter}});
    _event(e, T0 + 2 * MIN, {{"person", LEAVES}}, {{"person", jitter}});
    _event(e, T0 + 3 * MIN, {{"person", LEAVES}}, {{"person", jitter}});

    // A restart: the anchors come back from the file.
    E r;
    r.from_json(e.to_json());
    RTF_ASSERT_EQUAL(r.anchor_count("c", T0 + 4 * MIN), (size_t)2);
    r.event_started("c");
    RTF_ASSERT_EQUAL((int)r.consider("c", "car", 0.9f, PARKED_CAR, T0 + 4 * MIN).verdict, (int)E::early_anchored);
    RTF_ASSERT_EQUAL((int)r.consider("c", "person", 0.9f, LEAVES, T0 + 4 * MIN).verdict, (int)E::early_anchored);

    // A file it can't read changes nothing; bad entries are skipped.
    r.from_json("not json");
    RTF_ASSERT_EQUAL(r.anchor_count("c", T0 + 4 * MIN), (size_t)2);
    r.from_json("[1, 2]");
    RTF_ASSERT_EQUAL(r.anchor_count("c", T0 + 4 * MIN), (size_t)2);
    r.from_json(R"({"c": [{"class": "car", "x1": 1}], "d": "x", "e": [{"class": "car", "x1": 400, "y1": 300, "x2": 560, "y2": 390, "sightings": 1, "strong": true, "last_seen": )" + to_string(T0) + "}]}");
    RTF_ASSERT_EQUAL(r.anchor_count("c", T0), (size_t)0);
    RTF_ASSERT_EQUAL(r.anchor_count("e", T0), (size_t)1);
}

void test_r_early_alerts::test_probe_confirms()
{
    motion_region bbox {200, 100, 200, 300, true};
    vector<motion_region> regions {{200, 100, 100, 300, true}};
    auto on_motion = box(220, 150, 50, 150);   // centre (245, 225): on the motion
    auto off_motion = box(500, 150, 50, 150);  // centre (525, 225): not

    RTF_ASSERT(probe_confirms({{"person", 0.7f, on_motion}}, bbox, regions));
    RTF_ASSERT(probe_confirms({{"car", 0.9f, on_motion}, {"person", 0.6f, on_motion}}, bbox, regions));
    RTF_ASSERT(!probe_confirms({{"person", 0.59f, on_motion}}, bbox, regions));
    RTF_ASSERT(!probe_confirms({{"person", 0.9f, off_motion}}, bbox, regions));
    // Vehicles and animals need MOG2's confirmation.
    RTF_ASSERT(!probe_confirms({{"car", 0.95f, on_motion}, {"dog", 0.95f, on_motion}}, bbox, regions));
    RTF_ASSERT(!probe_confirms({}, bbox, regions));
    // No motion evidence at all: no.
    RTF_ASSERT(!probe_confirms({{"person", 0.9f, on_motion}}, {0, 0, 0, 0, false}, {}));
}
