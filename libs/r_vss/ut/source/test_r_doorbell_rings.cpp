#include "test_r_doorbell_rings.h"
#include "r_vss/r_doorbell_rings.h"

using namespace r_vss;
using namespace r_disco;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_doorbell_rings);

// One ring per press: presses within 5 s of the last ring are that ring, each doorbell on its own.
void test_r_doorbell_rings::test_debounce()
{
    r_doorbell_rings rings;
    RTF_ASSERT(rings.press("door", 10000));
    RTF_ASSERT(!rings.press("door", 10001));
    RTF_ASSERT(!rings.press("door", 14999));
    RTF_ASSERT(rings.press("door", 15000));          // 5 s after the ring
    RTF_ASSERT(!rings.press("door", 19999));         // measured from the last ring, not the last press
    RTF_ASSERT(rings.press("side", 16000));          // another doorbell
    RTF_ASSERT(rings.press("door", 20000));
    // A clock that went back: a press before the last ring rings.
    RTF_ASSERT(rings.press("door", 1000));
    rings.forget("door");
    RTF_ASSERT(rings.press("door", 1001));
}

void test_r_doorbell_rings::test_only_a_doorbells_press_rings()
{
    r_camera door;
    door.id = "door";
    door.model = string("Reolink Video Doorbell PoE");   // a doorbell by its model, nobody chose
    r_camera porch;
    porch.id = "porch";
    porch.model = string("RLC-810A");
    r_camera chosen = porch;
    chosen.id = "chosen";
    chosen.kind = string("doorbell");                     // a doorbell by choice
    r_camera not_door = door;
    not_door.id = "not_door";
    not_door.kind = string("camera");                     // chosen not to be one

    r_doorbell_rings rings;
    auto p = [&](const r_nullable<r_camera>& c, const string& what, bool active, int64_t now) {
        return doorbell_press(rings, c, what, active, now);
    };
    RTF_ASSERT(p(door, "visitor", true, 1000) == r_doorbell_press::ring);
    RTF_ASSERT(p(door, "visitor", true, 2000) == r_doorbell_press::repeat);
    RTF_ASSERT(p(door, "visitor", false, 9000) == r_doorbell_press::none);   // its end is nothing
    RTF_ASSERT(p(door, "person", true, 9000) == r_doorbell_press::none);     // nor any other class
    RTF_ASSERT(p(door, "motion", true, 9000) == r_doorbell_press::none);
    RTF_ASSERT(p(door, "visitor", true, 9000) == r_doorbell_press::ring);
    RTF_ASSERT(p(chosen, "visitor", true, 1000) == r_doorbell_press::ring);
    RTF_ASSERT(p(porch, "visitor", true, 1000) == r_doorbell_press::none);   // a camera's visitor event isn't a ring
    RTF_ASSERT(p(not_door, "visitor", true, 1000) == r_doorbell_press::none);
    RTF_ASSERT(p(r_nullable<r_camera>(), "visitor", true, 1000) == r_doorbell_press::none);   // gone
}

// The same press told twice, by ONVIF and by email (late, with the press's own time): one ring. Apart: two.
void test_r_doorbell_rings::test_one_ring_across_sources()
{
    r_doorbell_rings rings;
    RTF_ASSERT(rings.press("door", 100000, "onvif"));
    RTF_ASSERT(!rings.press("door", 99000, "reolink_email"));    // a second before, by its subject's time
    RTF_ASSERT(!rings.press("door", 114999, "reolink_email"));   // up to 15 s after
    RTF_ASSERT(rings.press("door", 115000, "reolink_email"));    // 15 s after: another press
    RTF_ASSERT(!rings.press("door", 101000 + 14000, "onvif"));   // within 15 s of that ring, before or after
    // The email first (ONVIF late or restarting), then ONVIF: still one.
    RTF_ASSERT(rings.press("side", 50000, "reolink_email"));
    RTF_ASSERT(!rings.press("side", 50500, "onvif"));
    // Same source: 5 s, as before.
    RTF_ASSERT(rings.press("side", 200000, "onvif"));
    RTF_ASSERT(!rings.press("side", 204999, "onvif"));
    RTF_ASSERT(rings.press("side", 205000, "onvif"));
}

// Each source's last press, ring or not, for saying which sources a doorbell's presses come by.
void test_r_doorbell_rings::test_last_press_by_source()
{
    r_doorbell_rings rings;
    RTF_ASSERT(rings.last_press("door", "onvif").is_null());
    rings.press("door", 1000, "reolink_email");
    rings.press("door", 1500, "onvif");                            // the same ring: still recorded
    RTF_ASSERT_EQUAL(rings.last_press("door", "reolink_email").value(), (int64_t)1000);
    RTF_ASSERT_EQUAL(rings.last_press("door", "onvif").value(), (int64_t)1500);
    RTF_ASSERT(rings.last_press("side", "onvif").is_null());
    rings.forget("door");
    RTF_ASSERT(rings.last_press("door", "onvif").is_null());
}

// A press by email counts only from a doorbell set to send them; ONVIF's always counts.
void test_r_doorbell_rings::test_a_source_the_doorbell_isnt_set_to()
{
    r_camera door;
    door.id = "door";
    door.kind = string("doorbell");
    r_doorbell_rings rings;
    RTF_ASSERT(doorbell_press(rings, door, "visitor", true, 1000, "reolink_email") == r_doorbell_press::none);
    RTF_ASSERT(doorbell_press(rings, door, "visitor", true, 1000, "onvif") == r_doorbell_press::ring);
    door.press_source = string("reolink_email");
    RTF_ASSERT(doorbell_press(rings, door, "visitor", true, 60000, "reolink_email") == r_doorbell_press::ring);
    RTF_ASSERT(doorbell_press(rings, door, "visitor", true, 61000, "onvif") == r_doorbell_press::repeat);
}

void test_r_doorbell_rings::test_ring_json()
{
    RTF_ASSERT_EQUAL(doorbell_ring_json(1795000000123), string("{\"ts\":1795000000123}"));
}
