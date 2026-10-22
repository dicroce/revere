#include "test_r_reolink.h"
#include "r_cameras/r_camera_events_chain.h"
#include "r_cameras/reolink/r_reolink_api.h"
#include "r_cameras/reolink/r_reolink_events.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace r_cameras;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_reolink);

// Shaped like Driveway's responses (2026-10-20, RLC-810A, ONVIF down at the time).
static const char* MD_QUIET = "[ { \"cmd\" : \"GetMdState\", \"code\" : 0, \"value\" : { \"state\" : 0 } } ]";
static const char* MD_MOTION = "[ { \"cmd\" : \"GetMdState\", \"code\" : 0, \"value\" : { \"state\" : 1 } } ]";
static const char* AI_QUIET =
    "[ { \"cmd\" : \"GetAiState\", \"code\" : 0, \"value\" : { \"channel\" : 0,"
    "      \"dog_cat\" : { \"alarm_state\" : 0, \"support\" : 1 },"
    "      \"face\" : { \"alarm_state\" : 0, \"support\" : 0 },"
    "      \"people\" : { \"alarm_state\" : 0, \"support\" : 1 },"
    "      \"vehicle\" : { \"alarm_state\" : 0, \"support\" : 1 } } } ]";
static const char* AI_PERSON =
    "[ { \"cmd\" : \"GetAiState\", \"code\" : 0, \"value\" : { \"channel\" : 0,"
    "      \"dog_cat\" : { \"alarm_state\" : 0, \"support\" : 1 },"
    "      \"people\" : { \"alarm_state\" : 1, \"support\" : 1 },"
    "      \"vehicle\" : { \"alarm_state\" : 0, \"support\" : 1 } } } ]";
// What a wrong password gets.
static const char* NOT_LOGGED_IN =
    "[ { \"cmd\" : \"GetMdState\", \"code\" : 1, \"error\" : { \"detail\" : \"please login first\", \"rspCode\" : -6 } } ]";

void test_r_reolink::test_parse_state()
{
    auto quiet = reolink_parse_state(MD_QUIET, AI_QUIET);
    RTF_ASSERT(!quiet.motion);
    RTF_ASSERT_EQUAL(quiet.ai.size(), (size_t)3);   // face isn't supported: left out
    RTF_ASSERT(quiet.ai.count("people") && !quiet.ai["people"]);
    RTF_ASSERT(!quiet.ai.count("face"));

    auto person = reolink_parse_state(MD_MOTION, AI_PERSON);
    RTF_ASSERT(person.motion);
    RTF_ASSERT(person.ai["people"]);
    RTF_ASSERT(!person.ai["vehicle"]);

    bool threw = false;
    try { reolink_parse_state(NOT_LOGGED_IN, AI_QUIET); }
    catch (const runtime_error& e) { threw = string(e.what()).find("please login first") != string::npos; }
    RTF_ASSERT(threw);

    threw = false;
    try { reolink_parse_state("<html>not the API</html>", AI_QUIET); }
    catch (const runtime_error&) { threw = true; }
    RTF_ASSERT(threw);
}

void test_r_reolink::test_state_tracker()
{
    r_reolink_state_tracker t;

    // The first poll is the state as found: all quiet, nothing starts.
    auto tr = t.process(reolink_parse_state(MD_QUIET, AI_QUIET));
    RTF_ASSERT(tr.empty());
    auto classes = t.classes();
    RTF_ASSERT_EQUAL(classes.size(), (size_t)3);

    tr = t.process(reolink_parse_state(MD_MOTION, AI_PERSON));
    RTF_ASSERT_EQUAL(tr.size(), (size_t)2);
    RTF_ASSERT_EQUAL(tr[0].what, string("motion"));
    RTF_ASSERT(tr[0].active);
    RTF_ASSERT_EQUAL(tr[1].what, string("person"));   // "people" in Revere's name
    RTF_ASSERT(tr[1].active);

    // The same state again: nothing.
    RTF_ASSERT(t.process(reolink_parse_state(MD_MOTION, AI_PERSON)).empty());

    // A person while the motion state stays 0 (Driveway, 2026-10-20): still motion.
    r_reolink_state_tracker u;
    u.process(reolink_parse_state(MD_QUIET, AI_QUIET));
    auto pt = u.process(reolink_parse_state(MD_QUIET, AI_PERSON));
    RTF_ASSERT_EQUAL(pt.size(), (size_t)2);
    RTF_ASSERT_EQUAL(pt[0].what, string("motion"));
    RTF_ASSERT(pt[0].active);
    RTF_ASSERT_EQUAL(pt[1].what, string("person"));
    pt = u.process(reolink_parse_state(MD_QUIET, AI_QUIET));
    RTF_ASSERT_EQUAL(pt.size(), (size_t)2);   // both end
    for (auto& x : pt)
        RTF_ASSERT(!x.active);

    // Polling fails: everything open ends.
    tr = t.close_all();
    RTF_ASSERT_EQUAL(tr.size(), (size_t)2);
    for (auto& x : tr)
        RTF_ASSERT(!x.active);
    RTF_ASSERT(t.close_all().empty());

    RTF_ASSERT_EQUAL(r_reolink_state_tracker::class_name("dog_cat"), string("animal"));
    RTF_ASSERT_EQUAL(r_reolink_state_tracker::class_name("vehicle"), string("vehicle"));
}

// A source whose status a test sets.
struct fake_events : public r_camera_events
{
    explicit fake_events(bool* up, int* started, int* destroyed, const string& source) :
        _up(up), _started(started), _destroyed(destroyed), _source(source) {}
    ~fake_events() noexcept override { if (_destroyed) ++*_destroyed; }
    void start() override { ++*_started; }
    void request_stop() override {}
    r_camera_events_status status() const override
    {
        r_camera_events_status s;
        s.source = _source;
        s.up = *_up;
        return s;
    }
    vector<r_camera_span> spans(int64_t) const override { return {{_source, 1, 2}}; }

    bool* _up;
    int* _started;
    int* _destroyed;
    string _source;
};

void test_r_reolink::test_chain_falls_back_and_returns()
{
    bool onvif_up = true, reolink_up = true;
    int onvif_started = 0, reolink_started = 0, reolink_destroyed = 0, probes = 0;

    r_camera_events_chain chain(
        [&](r_camera_events::transition_fn) -> unique_ptr<r_camera_events> { return make_unique<fake_events>(&onvif_up, &onvif_started, nullptr, "onvif"); },
        [&](r_camera_events::transition_fn) -> unique_ptr<r_camera_events> { return make_unique<fake_events>(&reolink_up, &reolink_started, &reolink_destroyed, "reolink"); },
        [&]() { ++probes; return true; },
        {}, "test");
    chain.start_without_thread();
    RTF_ASSERT_EQUAL(onvif_started, 1);

    chain.tick(1000);
    RTF_ASSERT(!chain.fallback_running());
    RTF_ASSERT_EQUAL(chain.status().source, string("onvif"));

    // ONVIF goes down: nothing until it has been down FALLBACK_AFTER_MS.
    onvif_up = false;
    chain.tick(2000);
    chain.tick(2000 + r_camera_events_chain::FALLBACK_AFTER_MS - 1);
    RTF_ASSERT(!chain.fallback_running());
    RTF_ASSERT_EQUAL(probes, 0);

    chain.tick(2000 + r_camera_events_chain::FALLBACK_AFTER_MS);
    RTF_ASSERT(chain.fallback_running());
    RTF_ASSERT_EQUAL(reolink_started, 1);
    RTF_ASSERT_EQUAL(probes, 1);
    RTF_ASSERT_EQUAL(chain.status().source, string("reolink"));
    RTF_ASSERT_EQUAL(chain.spans(0).size(), (size_t)2);   // both sources'

    // Still down: the fallback carries on, no second one.
    chain.tick(100000);
    RTF_ASSERT_EQUAL(reolink_started, 1);

    // ONVIF is back: the fallback stops; its spans are kept.
    onvif_up = true;
    chain.tick(110000);
    RTF_ASSERT(!chain.fallback_running());
    RTF_ASSERT_EQUAL(reolink_destroyed, 1);
    RTF_ASSERT_EQUAL(chain.status().source, string("onvif"));
    RTF_ASSERT_EQUAL(chain.spans(0).size(), (size_t)2);

    // Down again: the probe already passed, no need to ask again.
    onvif_up = false;
    chain.tick(120000);
    chain.tick(120000 + r_camera_events_chain::FALLBACK_AFTER_MS);
    RTF_ASSERT(chain.fallback_running());
    RTF_ASSERT_EQUAL(probes, 1);
    RTF_ASSERT_EQUAL(reolink_started, 2);
}

void test_r_reolink::test_chain_probe_fails()
{
    bool onvif_up = false, reolink_up = true;
    int onvif_started = 0, reolink_started = 0, probes = 0;

    r_camera_events_chain chain(
        [&](r_camera_events::transition_fn) -> unique_ptr<r_camera_events> { return make_unique<fake_events>(&onvif_up, &onvif_started, nullptr, "onvif"); },
        [&](r_camera_events::transition_fn) -> unique_ptr<r_camera_events> { return make_unique<fake_events>(&reolink_up, &reolink_started, nullptr, "reolink"); },
        [&]() { ++probes; return false; },   // not a Reolink
        {}, "test");
    chain.start_without_thread();

    chain.tick(0);
    chain.tick(r_camera_events_chain::FALLBACK_AFTER_MS);
    RTF_ASSERT_EQUAL(probes, 1);
    RTF_ASSERT(!chain.fallback_running());

    // Not asked again until PROBE_EVERY_MS has passed.
    chain.tick(r_camera_events_chain::FALLBACK_AFTER_MS + 10000);
    RTF_ASSERT_EQUAL(probes, 1);
    chain.tick(r_camera_events_chain::FALLBACK_AFTER_MS + r_camera_events_chain::PROBE_EVERY_MS);
    RTF_ASSERT_EQUAL(probes, 2);
    RTF_ASSERT_EQUAL(reolink_started, 0);
    RTF_ASSERT_EQUAL(chain.status().source, string("onvif"));
}
