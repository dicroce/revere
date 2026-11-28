
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

void test_r_early_alerts::test_parked_vehicle_left_out_of_the_record()
{
    // The event's start frame: the car already on its spot, or a cat or person at theirs.
    const vector<E::r_seen> ON_SPOT = {{"car", PARKED_CAR}};
    const vector<E::r_seen> CAT_THERE = {{"cat", LEAVES}};
    const vector<E::r_seen> PERSON_THERE = {{"person", LEAVES}};
    E e;
    // Nothing learnt yet: a car is no parked vehicle.
    RTF_ASSERT(!e.is_parked_vehicle("c", "car", PARKED_CAR, T0, ON_SPOT));
    // A parked car seen in both endpoint frames of an event becomes an anchor.
    RTF_ASSERT(_event(e, T0, {{"car", PARKED_CAR}}, {{"car", shift(PARKED_CAR, 2, 1)}}));
    // Later, YOLO's box for it (a little off, any frame of the event) is left out of the record...
    RTF_ASSERT(e.is_parked_vehicle("c", "car", shift(PARKED_CAR, -6, 4), T0 + HOUR, ON_SPOT));
    // ...also under another vehicle word, no: the anchor is the class's own.
    RTF_ASSERT(!e.is_parked_vehicle("c", "truck", PARKED_CAR, T0 + HOUR, ON_SPOT));
    // A car driving past elsewhere, or pulling in beside it, is kept.
    RTF_ASSERT(!e.is_parked_vehicle("c", "car", box(20, 330, 150, 80), T0 + HOUR, ON_SPOT));
    RTF_ASSERT(!e.is_parked_vehicle("c", "car", shift(PARKED_CAR, 90, 0), T0 + HOUR, ON_SPOT));
    // Another camera's anchors aren't this one's.
    RTF_ASSERT(!e.is_parked_vehicle("other", "car", PARKED_CAR, T0 + HOUR, ON_SPOT));
    // The anchor lapses a day after it was last seen.
    RTF_ASSERT(!e.is_parked_vehicle("c", "car", PARKED_CAR, T0 + 25 * HOUR, ON_SPOT));

    // Animals and persons are never left out for sitting where one sat before: a feeding station, a doorstep.
    RTF_ASSERT(_event(e, T0, {{"cat", LEAVES}}, {{"cat", shift(LEAVES, 1, 1)}}));
    RTF_ASSERT(!e.is_parked_vehicle("c", "cat", LEAVES, T0 + HOUR, CAT_THERE));
    RTF_ASSERT(_event(e, T0, {{"person", LEAVES}}, {{"person", shift(LEAVES, 1, 1)}}));
    RTF_ASSERT(!e.is_parked_vehicle("c", "person", LEAVES, T0 + HOUR, PERSON_THERE));

    // The anchor outlives the car, so a car arriving into the spot (or a visitor taking it) is kept: the start
    // frame showed the spot empty, or something else on it.
    RTF_ASSERT(e.is_parked_vehicle("c", "car", PARKED_CAR, T0 + HOUR, ON_SPOT));
    RTF_ASSERT(!e.is_parked_vehicle("c", "car", PARKED_CAR, T0 + HOUR, {}));
    RTF_ASSERT(!e.is_parked_vehicle("c", "car", PARKED_CAR, T0 + HOUR, {{"car", box(20, 330, 150, 80)}}));
    RTF_ASSERT(!e.is_parked_vehicle("c", "car", PARKED_CAR, T0 + HOUR, {{"truck", PARKED_CAR}}));
    // A start-frame car that only overlaps the spot a little (a car pulled in beside it) doesn't count either.
    RTF_ASSERT(!e.is_parked_vehicle("c", "car", PARKED_CAR, T0 + HOUR, {{"car", shift(PARKED_CAR, 90, 0)}}));
    // Nor does a truck detection take a car's anchor, even with a truck on the spot at the start.
    RTF_ASSERT(!e.is_parked_vehicle("c", "truck", PARKED_CAR, T0 + HOUR, {{"truck", PARKED_CAR}}));

    // A car seen once moving a little between the endpoint frames (a weak sighting) isn't parked yet; a second makes it so.
    E w;
    RTF_ASSERT(_event(w, T0, {{"car", PARKED_CAR}}, {{"car", shift(PARKED_CAR, 24, 0)}}));
    RTF_ASSERT(!w.is_parked_vehicle("c", "car", PARKED_CAR, T0 + HOUR, ON_SPOT));
    RTF_ASSERT(_event(w, T0 + HOUR, {{"car", PARKED_CAR}}, {{"car", shift(PARKED_CAR, 24, 0)}}));
    RTF_ASSERT(w.is_parked_vehicle("c", "car", PARKED_CAR, T0 + 2 * HOUR, ON_SPOT));
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

void test_r_early_alerts::test_once_per_class_across_a_chain()
{
    // Motion that goes on past the max event length is a chain of events (motion_event_continue): a class the chain
    // already sent early isn't sent again in a later event of it, and a class new to the chain still is. A new
    // chain (a plain start) sends again.
    E e;
    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.9f, LEAVES, T0).verdict, (int)E::early_sent);
    e.event_started("c", true);
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.9f, LEAVES, T0 + 5 * MIN).verdict, (int)E::early_already_sent);
    RTF_ASSERT(!e.sent_early("c", "person", T0 + 5 * MIN));
    RTF_ASSERT_EQUAL((int)e.consider("c", "car", 0.8f, PARKED_CAR, T0 + 6 * MIN).verdict, (int)E::early_sent);
    RTF_ASSERT(e.sent_early("c", "car", T0 + 6 * MIN));
    e.event_started("c", true);
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.9f, LEAVES, T0 + 10 * MIN).verdict, (int)E::early_already_sent);
    RTF_ASSERT_EQUAL((int)e.consider("c", "car", 0.8f, PARKED_CAR, T0 + 10 * MIN).verdict, (int)E::early_already_sent);
    // The detections themselves are per event: the first event's person isn't "sent early" in the third.
    RTF_ASSERT(!e.sent_early("c", "person", T0));

    e.event_started("c");
    RTF_ASSERT_EQUAL((int)e.consider("c", "person", 0.9f, LEAVES, T0 + 20 * MIN).verdict, (int)E::early_sent);

    // A continue on a camera with no event before it is a plain start.
    e.event_started("d", true);
    RTF_ASSERT_EQUAL((int)e.consider("d", "person", 0.9f, LEAVES, T0).verdict, (int)E::early_sent);
}

void test_r_early_alerts::test_log_notes()
{
    // A box for the log: corners and size, whole pixels.
    RTF_ASSERT_EQUAL(box_text(box(400, 300, 160, 90)), string("[400,300 560,390] 160x90"));
    RTF_ASSERT_EQUAL(box_text({10.4f, 20.6f, 30.5f, 41.0f}), string("[10,21 31,41] 20x20"));

    E e;
    // Nothing learnt: no anchor of the class.
    RTF_ASSERT_EQUAL(e.nearest_anchor("c", "car", PARKED_CAR, T0), string("no car anchor"));

    // Each person, vehicle or animal in the start frame gets a note; other classes don't.
    const r_box FAR_CAR = box(300, 200, 20, 12);
    vector<string> notes;
    e.event_started("c");
    const r_box TRUCK = box(100, 400, 60, 40);
    RTF_ASSERT(e.event_ended("c", T0, {{"car", PARKED_CAR}, {"car", FAR_CAR}, {"cat", LEAVES}, {"pottedplant", LEAVES}, {"dog", LEAVES}, {"truck", TRUCK}},
                             {{"car", PARKED_CAR}, {"car", shift(FAR_CAR, 8, 0)}, {"cat", shift(LEAVES, 12, 0)}, {"truck", shift(TRUCK, 30, 0)}}, &notes));
    RTF_ASSERT_EQUAL((int)notes.size(), 5);
    RTF_ASSERT_EQUAL(notes[0], string("car [400,300 560,390] 160x90: end-frame IoU 1.00: new anchor: 1 sighting, strong, active"));
    // A small far car whose box moved 8 px (of its 20): IoU 0.43, but jitter in place, so a sighting.
    RTF_ASSERT_EQUAL(notes[1], string("car [300,200 320,212] 20x12: end-frame IoU 0.43, a small box jittering in place: new anchor: 1 sighting, weak, not active"));
    RTF_ASSERT_EQUAL(notes[2], string("cat [60,120 100,210] 40x90: end-frame IoU 0.54: new anchor: 1 sighting, weak, not active"));
    RTF_ASSERT_EQUAL(notes[3], string("dog [60,120 100,210] 40x90: no dog in the end frame: not learnt"));
    // A truck that moved 30 px: neither.
    RTF_ASSERT_EQUAL(notes[4], string("truck [100,400 160,440] 60x40: end frame's best truck IoU 0.33, under 0.50: not learnt"));

    // Seen again in a later event: the anchor's count and state.
    notes.clear();
    e.event_started("c");
    e.event_ended("c", T0 + HOUR, {{"cat", LEAVES}}, {{"cat", shift(LEAVES, 12, 0)}}, &notes);
    RTF_ASSERT_EQUAL((int)notes.size(), 1);
    RTF_ASSERT_EQUAL(notes[0], string("cat [60,120 100,210] 40x90: end-frame IoU 0.54: anchor seen again (IoU 1.00): 2 sightings, weak, active"));

    // The nearest anchor of the class, active or not; another class's anchors don't count.
    RTF_ASSERT_EQUAL(e.nearest_anchor("c", "car", shift(PARKED_CAR, 16, 0), T0 + HOUR),
                     string("nearest car anchor [400,300 560,390] 160x90: IoU 0.82, 1 sighting, strong, active"));
    RTF_ASSERT_EQUAL(e.nearest_anchor("c", "car", PARKED_CAR, T0 + 25 * HOUR),
                     string("nearest car anchor [400,300 560,390] 160x90: IoU 1.00, 1 sighting, strong, not active"));
    RTF_ASSERT_EQUAL(e.nearest_anchor("c", "truck", PARKED_CAR, T0 + HOUR), string("no truck anchor"));
    RTF_ASSERT_EQUAL(e.nearest_anchor("other", "car", PARKED_CAR, T0 + HOUR), string("no car anchor"));
    // With two car anchors, the one that overlaps best, whichever was learnt first.
    const r_box SECOND_CAR = box(20, 330, 150, 80);
    e.event_started("c");
    e.event_ended("c", T0 + HOUR, {{"car", SECOND_CAR}}, {{"car", SECOND_CAR}});
    RTF_ASSERT_EQUAL(e.nearest_anchor("c", "car", shift(SECOND_CAR, 4, 0), T0 + HOUR).substr(0, 42),
                     string("nearest car anchor [20,330 170,410] 150x80"));
    // Without a notes list, learning is as before.
    RTF_ASSERT(_event(e, T0 + 2 * HOUR, {{"car", PARKED_CAR}}, {{"car", PARKED_CAR}}));
}

void test_r_early_alerts::test_small_parked_car()
{
    // YOLO's jitter on a far car's small box: the same place, though IoU says otherwise.
    const r_box FAR_CAR = box(300, 200, 20, 12);
    RTF_ASSERT(box_iou(FAR_CAR, shift(FAR_CAR, 8, 0)) < 0.5f);
    RTF_ASSERT(same_place("car", FAR_CAR, shift(FAR_CAR, 8, 0), 0.5f));
    RTF_ASSERT(same_place("truck", FAR_CAR, shift(FAR_CAR, -8, 6), 0.5f));
    RTF_ASSERT(!same_place("car", FAR_CAR, shift(FAR_CAR, 9, 0), 0.5f));
    // Half the box, on a short side: 6 px of its 12.
    RTF_ASSERT(!same_place("car", FAR_CAR, shift(FAR_CAR, 0, 7), 0.5f));
    // A box much bigger or smaller isn't the same car, even on the same centre (each of these has IoU under 0.5).
    RTF_ASSERT(!same_place("car", FAR_CAR, {294.5f, 197.5f, 325.5f, 214.5f}, 0.5f));   // 31x17: too wide
    RTF_ASSERT(!same_place("car", FAR_CAR, {295.5f, 196.5f, 324.5f, 215.5f}, 0.5f));   // 29x19: too tall
    RTF_ASSERT(!same_place("car", {294.5f, 197.5f, 325.5f, 214.5f}, FAR_CAR, 0.5f));   // either way round
    RTF_ASSERT(box_iou(FAR_CAR, box(300, 200, 30, 18)) < 0.5f);
    RTF_ASSERT(same_place("car", FAR_CAR, box(300, 200, 30, 18), 0.5f));               // 1.5 times: still the car
    // Persons and animals: IoU only.
    RTF_ASSERT(!same_place("person", FAR_CAR, shift(FAR_CAR, 8, 0), 0.5f));
    RTF_ASSERT(!same_place("cat", FAR_CAR, shift(FAR_CAR, 8, 0), 0.5f));
    RTF_ASSERT(same_place("person", PARKED_CAR, shift(PARKED_CAR, 8, 0), 0.5f));
    // A big car that drove on, and an empty box, aren't.
    RTF_ASSERT(!same_place("car", PARKED_CAR, shift(PARKED_CAR, 70, 0), 0.5f));
    RTF_ASSERT(!same_place("car", box(300, 200, 0, 12), box(300, 200, 0, 12), 0.5f));

    // Two events with the far car jittering between their start and end frames: an active (weak) anchor.
    E e;
    _event(e, T0, {{"car", FAR_CAR}}, {{"car", shift(FAR_CAR, 7, 1)}});
    RTF_ASSERT(e.anchor_count("c", T0) == 0);
    // Its start-frame box this time is 8 px from the first's (IoU 0.43): the same anchor, seen again.
    RTF_ASSERT(box_iou(FAR_CAR, shift(FAR_CAR, -8, 0)) < 0.5f);
    _event(e, T0 + HOUR, {{"car", shift(FAR_CAR, -8, 0)}}, {{"car", shift(FAR_CAR, -1, -2)}});
    RTF_ASSERT(e.anchor_count("c", T0 + HOUR) == 1);

    // Then a later sighting of it, a few px off (the anchor keeps its latest start-frame box: 8 px left), is held
    // from the early alert and left out of the record.
    const r_box LATER = shift(FAR_CAR, -4, 2);
    e.event_started("c");
    auto d = e.anchored("c", "car", LATER, T0 + 2 * HOUR);
    RTF_ASSERT_EQUAL((int)d.verdict, (int)E::early_anchored);
    RTF_ASSERT(!d.strong_anchor);
    RTF_ASSERT_EQUAL((int)e.consider("c", "car", 0.91f, LATER, T0 + 2 * HOUR).verdict, (int)E::early_anchored);
    RTF_ASSERT(e.is_parked_vehicle("c", "car", LATER, T0 + 2 * HOUR, {{"car", shift(FAR_CAR, -3, 0)}}));
    // A start frame whose box for it is 8 px off the anchor's (IoU 0.43) still shows it parked.
    RTF_ASSERT(e.is_parked_vehicle("c", "car", LATER, T0 + 2 * HOUR, {{"car", FAR_CAR}}));
    // anchored() records nothing: the car still isn't sent this event, nor marked so.
    RTF_ASSERT(!e.sent_early("c", "car", T0 + 2 * HOUR));
    // Not on it: elsewhere, another class, a class that doesn't alert, or once it has lapsed.
    RTF_ASSERT_EQUAL((int)e.anchored("c", "car", shift(FAR_CAR, 40, 0), T0 + 2 * HOUR).verdict, (int)E::early_not_alerting);
    RTF_ASSERT_EQUAL((int)e.anchored("c", "truck", LATER, T0 + 2 * HOUR).verdict, (int)E::early_not_alerting);
    RTF_ASSERT_EQUAL((int)e.anchored("c", "pottedplant", LATER, T0 + 2 * HOUR).verdict, (int)E::early_not_alerting);
    RTF_ASSERT_EQUAL((int)e.anchored("c", "car", LATER, T0 + HOUR + 25 * HOUR).verdict, (int)E::early_not_alerting);
    // Of two anchors it's on (IoU 0.56 with the first learnt, 0.73 with the second), the better one's IoU.
    _event(e, T0 + 2 * HOUR, {{"car", PARKED_CAR}}, {{"car", PARKED_CAR}});
    _event(e, T0 + 2 * HOUR, {{"car", shift(PARKED_CAR, 70, 0)}}, {{"car", shift(PARKED_CAR, 70, 0)}});
    auto two = e.anchored("c", "car", shift(PARKED_CAR, 45, 0), T0 + 2 * HOUR);
    RTF_ASSERT(two.anchor_iou > 0.7f && two.anchor_iou < 0.75f);
}
