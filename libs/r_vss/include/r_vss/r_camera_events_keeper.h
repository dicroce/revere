#ifndef __r_vss_r_camera_events_keeper_h
#define __r_vss_r_camera_events_keeper_h

#include "r_cameras/r_camera_events.h"
#include "r_disco/r_camera.h"
#include "r_utils/r_macro.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace r_vss
{

// Each camera's event subscription, kept for as long as the camera is, across its stream's reconnects. A camera
// keeps only a few subscriptions, and one made for every stream restart left them used up; the stream and the events
// are separate connections, and one dropping says nothing about the other.
//
// The camera's recording context attaches to it while it's up. It's told what's open as it attaches (a start for
// each), what changes while it's attached, and an end for each open one as it detaches, so its motion window opens
// and closes as it did when it had a subscription of its own. Every transition also goes to `always` (the stream
// keeper's), attached or not.
//
// A subscription is replaced when the camera's settings it depends on change (settings_key), and ended when the
// camera goes. Calls that end one hand it back, to be destroyed (which waits for its thread) outside the caller's
// locks.
class r_camera_events_keeper
{
public:
    using events_ptr = std::unique_ptr<r_cameras::r_camera_events>;
    using factory_fn = std::function<events_ptr(const r_disco::r_camera&, r_cameras::r_camera_events::transition_fn)>;
    using always_fn = std::function<void(const std::string& camera_id, const r_cameras::r_camera_transition&)>;
    using sink_fn = r_cameras::r_camera_events::transition_fn;

    R_API r_camera_events_keeper(factory_fn factory, always_fn always);
    // Stops and waits for every subscription.
    R_API ~r_camera_events_keeper() noexcept;

    r_camera_events_keeper(const r_camera_events_keeper&) = delete;
    r_camera_events_keeper& operator=(const r_camera_events_keeper&) = delete;

    // The camera has a subscription, started now if it had none or its settings changed (none when the factory makes
    // none for them). The one replaced, if any, comes back (already told to stop) after telling its attached context
    // it's all over: that context's sink is called here, so call this outside any lock the sink might take.
    R_API std::vector<events_ptr> ensure(const r_disco::r_camera& camera);

    // From now on `owner`'s sink gets the camera's transitions, first a start for each event open now. Nothing when the
    // camera has no subscription. A second attach replaces the first, which is detached.
    R_API void attach(const std::string& camera_id, const void* owner, sink_fn sink);

    // `owner` (when it's still the one attached) gets an end for each open event, and nothing after this returns.
    R_API void detach(const std::string& camera_id, const void* owner);

    // The camera's subscription ends: its attached context is detached first. Null when it had none.
    R_API events_ptr drop(const std::string& camera_id);
    // Every subscription of a camera not in `keep` ends (cameras gone).
    R_API std::vector<events_ptr> drop_except(const std::set<std::string>& keep);

    // Asks every subscription to stop, without waiting (a shutdown).
    R_API void request_stop_all();
    // Ends and waits for every subscription.
    R_API void clear();

    R_API bool has(const std::string& camera_id) const;
    R_API r_cameras::r_camera_events_status status(const std::string& camera_id) const;
    // The camera's spans that started at or after since_ms (r_camera_events::spans); none without a subscription.
    R_API std::vector<r_cameras::r_camera_span> spans(const std::string& camera_id, int64_t since_ms) const;

    // What a subscription depends on: where the camera is and how to sign in. A change in any makes a new one.
    R_API static std::string settings_key(const r_disco::r_camera& camera);

private:
    struct slot
    {
        std::string camera_id;
        std::string key;
        // Held while a sink is called, so a detach returns only once no call to its sink is under way.
        std::mutex deliver;
        // Guards what follows.
        std::mutex lock;
        events_ptr events;   // moved out (under lock) when the subscription ends
        const void* owner {nullptr};
        sink_fn sink;
        std::set<std::pair<std::string, std::string>> open;   // (what, source) started and not ended
    };

    static void _deliver(const std::weak_ptr<slot>& w, const r_cameras::r_camera_transition& t);
    static void _detach(slot& s);
    std::shared_ptr<slot> _find(const std::string& camera_id) const;

    factory_fn _factory;
    always_fn _always;
    mutable std::mutex _lock;
    std::map<std::string, std::shared_ptr<slot>> _slots;
};

}

#endif
