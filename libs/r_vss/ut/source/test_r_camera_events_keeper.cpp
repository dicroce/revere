#include "test_r_camera_events_keeper.h"
#include "r_vss/r_camera_events_keeper.h"
#include <memory>
#include <string>
#include <vector>

using namespace r_vss;
using namespace r_cameras;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_camera_events_keeper);

namespace
{

// Events a test drives by hand: what it emits goes to the keeper's callback, as a real subscription's would.
struct fake_events : public r_camera_events
{
    transition_fn fn;
    bool started {false};
    bool stop_asked {false};
    bool* destroyed;
    explicit fake_events(transition_fn f, bool* d) : fn(std::move(f)), destroyed(d) {}
    ~fake_events() noexcept override { if (destroyed) *destroyed = true; }
    void start() override { started = true; }
    void request_stop() override { stop_asked = true; }
    r_camera_events_status status() const override { r_camera_events_status s; s.up = started && !stop_asked; s.state = "subscribed"; return s; }
    vector<r_camera_span> spans(int64_t) const override { return {}; }
};

struct rig
{
    vector<fake_events*> made;          // every subscription made, newest last (owned by the keeper)
    vector<unique_ptr<bool>> destroyed; // whether each was destroyed
    vector<string> always;              // "<camera> <what> on|off", everything
    unique_ptr<r_camera_events_keeper> keeper;

    rig()
    {
        keeper = make_unique<r_camera_events_keeper>(
            [this](const r_disco::r_camera& camera, r_camera_events::transition_fn t) -> unique_ptr<r_camera_events> {
                if (camera.ipv4.is_null())
                    return nullptr;
                destroyed.push_back(make_unique<bool>(false));
                auto e = make_unique<fake_events>(std::move(t), destroyed.back().get());
                made.push_back(e.get());
                return e;
            },
            [this](const string& id, const r_camera_transition& t) { always.push_back(id + " " + t.what + (t.active ? " on" : " off")); });
    }
    void emit(const string& what, bool active, const string& source = "")
    {
        r_camera_transition t;
        t.what = what;
        t.active = active;
        t.source = source;
        made.back()->fn(t);
    }
};

r_disco::r_camera camera(const string& password = "pw")
{
    r_disco::r_camera c;
    c.id = "cam";
    c.ipv4 = string("10.0.0.5");
    c.xaddrs = string("http://10.0.0.5/onvif/device_service");
    c.rtsp_username = string("admin");
    c.rtsp_password = password;
    return c;
}

// A recording context's view: what it was told, in order.
struct sink
{
    vector<string> got;
    r_camera_events_keeper::sink_fn fn()
    {
        return [this](const r_camera_transition& t) { got.push_back(t.what + (t.source.empty() ? "" : "/" + t.source) + (t.active ? " on" : " off")); };
    }
};

}

void test_r_camera_events_keeper::test_a_reconnect_keeps_the_subscription()
{
    // The stream reconnects: the old context detaches, a new one attaches. The camera's subscription is the same one.
    rig r;
    RTF_ASSERT(r.keeper->ensure(camera()).empty());
    sink first, second;
    r.keeper->attach("cam", &first, first.fn());
    r.keeper->detach("cam", &first);
    RTF_ASSERT(r.keeper->ensure(camera()).empty());
    r.keeper->attach("cam", &second, second.fn());
    RTF_ASSERT_EQUAL(r.made.size(), (size_t)1);
    RTF_ASSERT(r.made[0]->started);
    RTF_ASSERT(!r.made[0]->stop_asked);
    RTF_ASSERT(r.keeper->has("cam"));
    RTF_ASSERT(r.keeper->status("cam").up);
}

void test_r_camera_events_keeper::test_attach_is_told_what_is_open_and_detach_that_it_ended()
{
    // Motion opened while no context was attached (between reconnects): the next one starts with it open, and the one
    // leaving is told it ended, so each context's motion window opens and closes as when it had its own subscription.
    rig r;
    r.keeper->ensure(camera());
    r.emit("motion", true);
    r.emit("person", true, "1");
    sink a;
    r.keeper->attach("cam", &a, a.fn());
    RTF_ASSERT_EQUAL(a.got.size(), (size_t)2);
    RTF_ASSERT(a.got[0] == "motion on" && a.got[1] == "person/1 on");
    r.emit("person", false, "1");
    r.keeper->detach("cam", &a);
    RTF_ASSERT_EQUAL(a.got.size(), (size_t)4);
    RTF_ASSERT_EQUAL(a.got[2], string("person/1 off"));
    RTF_ASSERT_EQUAL(a.got[3], string("motion off"));   // still open at the camera: ended for this context only
    // Nothing reaches it after the detach.
    r.emit("motion", false);
    RTF_ASSERT_EQUAL(a.got.size(), (size_t)4);
    // The next one finds nothing open.
    sink b;
    r.keeper->attach("cam", &b, b.fn());
    RTF_ASSERT(b.got.empty());
}

void test_r_camera_events_keeper::test_transitions_reach_the_attached_and_always()
{
    rig r;
    r.keeper->ensure(camera());
    r.emit("motion", true);   // no context: only always
    sink a;
    r.keeper->attach("cam", &a, a.fn());
    r.emit("motion", false);
    RTF_ASSERT_EQUAL(r.always.size(), (size_t)2);
    RTF_ASSERT(r.always[0] == "cam motion on" && r.always[1] == "cam motion off");
    // The detach's own "off" is the context's alone: the camera's state didn't change.
    r.emit("vehicle", true);
    r.keeper->detach("cam", &a);
    RTF_ASSERT_EQUAL(r.always.size(), (size_t)3);
    RTF_ASSERT_EQUAL(a.got.back(), string("vehicle off"));
}

void test_r_camera_events_keeper::test_new_settings_make_a_new_subscription()
{
    // A new password: the old subscription is stopped and handed back to be destroyed, its context told it all ended,
    // and a new one started.
    rig r;
    r.keeper->ensure(camera("old"));
    r.emit("motion", true);
    sink a;
    r.keeper->attach("cam", &a, a.fn());
    auto replaced = r.keeper->ensure(camera("new"));
    RTF_ASSERT_EQUAL(replaced.size(), (size_t)1);
    RTF_ASSERT_EQUAL(r.made.size(), (size_t)2);
    RTF_ASSERT(r.made[0]->stop_asked);
    RTF_ASSERT(!*r.destroyed[0]);   // the caller destroys it, outside its locks
    replaced.clear();
    RTF_ASSERT(*r.destroyed[0]);
    RTF_ASSERT(r.made[1]->started);
    RTF_ASSERT_EQUAL(a.got.back(), string("motion off"));
    // Its transitions don't reach the old context; it attaches again to hear them.
    r.emit("motion", true);
    RTF_ASSERT_EQUAL(a.got.size(), (size_t)2);
    RTF_ASSERT(r.keeper->settings_key(camera("a")) != r.keeper->settings_key(camera("b")));
}

void test_r_camera_events_keeper::test_a_camera_that_goes_ends_its_subscription()
{
    rig r;
    r.keeper->ensure(camera());
    auto other = camera();
    other.id = "cam2";
    r.keeper->ensure(other);
    r.emit("motion", true);   // cam2's
    sink a;
    r.keeper->attach("cam2", &a, a.fn());
    auto gone = r.keeper->drop_except({"cam"});
    RTF_ASSERT_EQUAL(gone.size(), (size_t)1);
    RTF_ASSERT(r.made[1]->stop_asked);
    RTF_ASSERT(!r.made[0]->stop_asked);
    RTF_ASSERT_EQUAL(a.got.back(), string("motion off"));
    RTF_ASSERT(!r.keeper->has("cam2"));
    RTF_ASSERT(!r.keeper->status("cam2").up);
    auto dropped = r.keeper->drop("cam");
    RTF_ASSERT((bool)dropped);
    RTF_ASSERT(!r.keeper->drop("cam"));
    RTF_ASSERT(!r.keeper->has("cam"));
}

void test_r_camera_events_keeper::test_only_the_attached_owner_detaches()
{
    // A context torn down after its replacement attached must not take the new one's events away.
    rig r;
    r.keeper->ensure(camera());
    sink old_one, new_one;
    r.keeper->attach("cam", &old_one, old_one.fn());
    r.keeper->attach("cam", &new_one, new_one.fn());
    r.keeper->detach("cam", &old_one);
    r.emit("motion", true);
    RTF_ASSERT_EQUAL(new_one.got.size(), (size_t)1);
    RTF_ASSERT(old_one.got.empty());
}

void test_r_camera_events_keeper::test_a_camera_without_events_has_none()
{
    rig r;
    auto c = camera();
    c.ipv4.clear();
    RTF_ASSERT(r.keeper->ensure(c).empty());
    RTF_ASSERT(!r.keeper->has("cam"));
    sink a;
    r.keeper->attach("cam", &a, a.fn());   // nothing to attach to: no harm
    r.keeper->detach("cam", &a);
    RTF_ASSERT(a.got.empty());
}
