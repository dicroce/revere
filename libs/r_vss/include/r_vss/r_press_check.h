#ifndef __r_vss_r_press_check_h
#define __r_vss_r_press_check_h

#include "r_disco/r_camera.h"
#include "r_utils/r_nullable.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace r_vss
{

// One source's part in a "Press the doorbell now" test: whether it reported the press, when the press was (its own
// time, for an email the time it gives), and when the report arrived.
struct r_press_test_source
{
    std::string source;     // "onvif", "reolink_email"
    bool seen {false};
    int64_t press_ms {0};
    int64_t arrived_ms {0};
};

// A doorbell's press test: open for r_press_check::TEST_MS from started_ms; the result is kept after it closes.
struct r_press_test
{
    bool any {false};           // a test was ever started for this doorbell (since revere started)
    bool open {false};
    int64_t started_ms {0};
    int64_t ends_ms {0};
    bool ring_phones {false};   // its press rings the phones anyway
    std::vector<r_press_test_source> sources;
    // The doorbell emailed something that isn't a press while the test was open: its email works, but it isn't set to
    // send presses (its Visitor alert), or this was the Reolink app's Test email.
    bool other_mail {false};
};

// Which of a doorbell's sources report its presses, so setup can say which to use:
// - a one-shot test: "Press the doorbell now" opens a window, and each source that reports a press within it is
//   marked, with how long its report took. While it's open the press doesn't ring the phones, unless the person
//   asked it to (the test is the person at the door).
// - the passive part, from r_doorbell_rings::last_press(): suggest_onvif() says when a doorbell set to email has had a
//   press by ONVIF, so instant rings are on offer.
// Safe from any thread.
class r_press_check
{
public:
    enum { TEST_MS = 60000 };

    // Starts (or starts again) the doorbell's test at now_ms, watching sources ("onvif" first).
    r_press_test start(const std::string& camera_id, int64_t now_ms, bool ring_phones, const std::vector<std::string>& sources)
    {
        std::lock_guard<std::mutex> g(_lock);
        r_press_test t;
        t.any = true;
        t.open = true;
        t.started_ms = now_ms;
        t.ends_ms = now_ms + TEST_MS;
        t.ring_phones = ring_phones;
        for(auto& s : sources)
            t.sources.push_back(r_press_test_source{s, false, 0, 0});
        _tests[camera_id] = t;
        return t;
    }

    // A press source reported, with the press's own time, at now_ms. True when its ring is held back from the phones:
    // a test is open for the doorbell and the person didn't ask for it to ring. A press from before the test opened
    // (an email for an earlier press, arriving late) isn't the test's.
    bool press(const std::string& camera_id, const std::string& source, int64_t press_ms, int64_t now_ms)
    {
        std::lock_guard<std::mutex> g(_lock);
        auto it = _tests.find(camera_id);
        if(it == _tests.end())
            return false;
        auto& t = it->second;
        _close_if_over(t, now_ms);
        if(!t.open || press_ms < t.started_ms)
            return false;
        for(auto& s : t.sources)
        {
            if(s.source == source && !s.seen)
            {
                s.seen = true;
                s.press_ms = press_ms;
                s.arrived_ms = now_ms;
            }
        }
        return !t.ring_phones;
    }

    // The doorbell emailed something that isn't a press, at now_ms: noted when its test is open.
    void other_mail(const std::string& camera_id, int64_t now_ms)
    {
        std::lock_guard<std::mutex> g(_lock);
        auto it = _tests.find(camera_id);
        if(it == _tests.end())
            return;
        _close_if_over(it->second, now_ms);
        if(it->second.open)
            it->second.other_mail = true;
    }

    // The doorbell's test as it is at now_ms: closed once TEST_MS have passed. any is false when it never had one.
    r_press_test test(const std::string& camera_id, int64_t now_ms)
    {
        std::lock_guard<std::mutex> g(_lock);
        auto it = _tests.find(camera_id);
        if(it == _tests.end())
            return r_press_test();
        _close_if_over(it->second, now_ms);
        return it->second;
    }

    // The camera is gone, or no longer a doorbell.
    void forget(const std::string& camera_id)
    {
        std::lock_guard<std::mutex> g(_lock);
        _tests.erase(camera_id);
    }

private:
    static void _close_if_over(r_press_test& t, int64_t now_ms)
    {
        if(t.open && now_ms >= t.ends_ms)
            t.open = false;
    }

    std::mutex _lock;
    std::map<std::string, r_press_test> _tests;
};

// The sources a doorbell's test watches: ONVIF, which always counts, and its other source when it has one.
inline std::vector<std::string> press_test_sources(const r_disco::r_camera& camera)
{
    std::vector<std::string> out{"onvif"};
    auto source = r_disco::camera_press_source(camera);
    if(source != "onvif")
        out.push_back(source);
    return out;
}

// Whether to suggest setting a doorbell back to ONVIF: it's set to another source, and ONVIF has now delivered a press
// of it (a firmware that sends it now), so its rings could come at once, not late.
inline bool suggest_onvif(const r_disco::r_camera& camera, const r_utils::r_nullable<int64_t>& last_onvif_press_ms)
{
    return r_disco::camera_kind(camera) == "doorbell" && r_disco::camera_press_source(camera) != "onvif" &&
           !last_onvif_press_ms.is_null();
}

}

#endif
