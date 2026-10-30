#include "test_r_teardown_queue.h"
#include "r_vss/r_teardown_queue.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <functional>

using namespace r_vss;
using namespace std;
using namespace std::chrono;

REGISTER_TEST_FIXTURE(test_r_teardown_queue);

namespace
{

// A gate a destructor can wait at, as a recording context's teardown does on
// a GStreamer state change that never completes. Shared, so a teardown left
// running after the test can still be let go.
struct gate
{
    mutex m;
    condition_variable cv;
    bool open {false};
    atomic<bool> entered {false};

    void wait()
    {
        entered = true;
        unique_lock<mutex> g(m);
        cv.wait(g, [this]() { return open; });
    }
    void release()
    {
        { lock_guard<mutex> g(m); open = true; }
        cv.notify_all();
    }
};

struct recorder
{
    mutex m;
    vector<string> destroyed;
    thread::id last_thread;
};

struct doomed
{
    string name;
    shared_ptr<recorder> rec;
    shared_ptr<gate> blocker;
    ~doomed()
    {
        if(blocker)
            blocker->wait();
        lock_guard<mutex> g(rec->m);
        rec->destroyed.push_back(name);
        rec->last_thread = this_thread::get_id();
    }
};

bool wait_until(const function<bool()>& f, milliseconds limit = milliseconds(2000))
{
    auto end = steady_clock::now() + limit;
    while(!f())
    {
        if(steady_clock::now() > end)
            return false;
        this_thread::sleep_for(milliseconds(5));
    }
    return true;
}

}

namespace
{

shared_ptr<doomed> make(const string& name, const shared_ptr<recorder>& rec, const shared_ptr<gate>& blocker = nullptr)
{
    auto obj = make_shared<doomed>();
    obj->name = name; obj->rec = rec; obj->blocker = blocker;
    return obj;
}

bool destroyed(const shared_ptr<recorder>& rec, const string& name)
{
    lock_guard<mutex> l(rec->m);
    for(auto& n : rec->destroyed)
        if(n == name)
            return true;
    return false;
}

}

void test_r_teardown_queue::test_destroys_each_once()
{
    auto rec = make_shared<recorder>();
    r_teardown_queue q;
    for(auto name : {"a", "b", "c"})
        q.push(name, name, make(name, rec));
    RTF_ASSERT(q.finish(seconds(2)));
    lock_guard<mutex> g(rec->m);
    // Each once, and not on the caller's thread.
    auto d = rec->destroyed;
    sort(d.begin(), d.end());
    RTF_ASSERT(d == vector<string>({"a", "b", "c"}));
    RTF_ASSERT(rec->last_thread != this_thread::get_id());
    RTF_ASSERT(!q.pending("a") && !q.pending("b") && !q.pending("c"));
}

void test_r_teardown_queue::test_pending_until_destroyed()
{
    auto rec = make_shared<recorder>();
    auto g = make_shared<gate>();
    r_teardown_queue q;
    q.push("cam", "Cam", make("cam", rec, g));

    RTF_ASSERT(wait_until([&]() { return g->entered.load(); }));
    // Being torn down: the keeper mustn't start the camera again yet.
    RTF_ASSERT(q.pending("cam"));
    RTF_ASSERT(!q.pending("other"));

    g->release();
    RTF_ASSERT(wait_until([&]() { return !q.pending("cam"); }));
    RTF_ASSERT(q.finish(seconds(2)));
}

void test_r_teardown_queue::test_stuck_teardown_doesnt_hold_the_others()
{
    auto rec = make_shared<recorder>();
    auto g = make_shared<gate>();
    r_teardown_queue q;
    q.push("stuck", "Stuck Cam", make("stuck", rec, g));
    RTF_ASSERT(wait_until([&]() { return g->entered.load(); }));

    // The caller (the keeper's thread) goes on, and so does the next camera's
    // teardown: it finishes while the first is still stuck.
    auto t0 = steady_clock::now();
    q.push("next", "Next Cam", make("next", rec));
    RTF_ASSERT(steady_clock::now() - t0 < milliseconds(200));
    RTF_ASSERT(wait_until([&]() { return destroyed(rec, "next"); }));
    RTF_ASSERT(!q.pending("next"));
    RTF_ASSERT(q.pending("stuck"));

    // The stuck one is reported with its name, how long, and that it is running.
    this_thread::sleep_for(milliseconds(60));
    auto s = q.stuck(50);
    RTF_ASSERT(s.size() == 1);
    RTF_ASSERT(s[0].key == "stuck");
    RTF_ASSERT(s[0].name == "Stuck Cam");
    RTF_ASSERT(s[0].ms >= 50);
    RTF_ASSERT(s[0].started);
    RTF_ASSERT(q.stuck(60000).empty());

    g->release();
    RTF_ASSERT(q.finish(seconds(2)));
    RTF_ASSERT(destroyed(rec, "stuck"));
}

void test_r_teardown_queue::test_waiting_behind_stuck_ones_is_reported()
{
    auto rec = make_shared<recorder>();
    vector<shared_ptr<gate>> gates;
    r_teardown_queue q;
    // Every thread stuck.
    for(int i = 0; i < r_teardown_queue::THREADS; ++i)
    {
        gates.push_back(make_shared<gate>());
        q.push("stuck" + to_string(i), "Stuck " + to_string(i), make("stuck" + to_string(i), rec, gates.back()));
    }
    RTF_ASSERT(wait_until([&]() { for(auto& g : gates) if(!g->entered) return false; return true; }));

    // The next waits for a thread, and says so: pending, not started.
    q.push("waiting", "Waiting Cam", make("waiting", rec));
    this_thread::sleep_for(milliseconds(60));
    RTF_ASSERT(q.pending("waiting"));
    bool seen = false;
    for(auto& s : q.stuck(50))
        if(s.key == "waiting")
        {
            seen = true;
            RTF_ASSERT(!s.started);
            RTF_ASSERT(s.name == "Waiting Cam");
        }
    RTF_ASSERT(seen);
    RTF_ASSERT(!destroyed(rec, "waiting"));

    // A thread freed takes it.
    gates[0]->release();
    RTF_ASSERT(wait_until([&]() { return destroyed(rec, "waiting"); }));
    for(auto& g : gates)
        g->release();
    RTF_ASSERT(q.finish(seconds(2)));
}

void test_r_teardown_queue::test_finish_is_bounded()
{
    auto rec = make_shared<recorder>();
    vector<shared_ptr<gate>> gates;
    {
        r_teardown_queue q;
        for(int i = 0; i < r_teardown_queue::THREADS; ++i)
        {
            gates.push_back(make_shared<gate>());
            q.push("stuck" + to_string(i), "Stuck " + to_string(i), make("stuck" + to_string(i), rec, gates.back()));
        }
        RTF_ASSERT(wait_until([&]() { for(auto& g : gates) if(!g->entered) return false; return true; }));
        q.push("queued", "Queued Cam", make("queued", rec));

        // revere quitting: the wait ends at its bound, not when the teardowns do.
        auto t0 = steady_clock::now();
        RTF_ASSERT(!q.finish(milliseconds(300)));
        auto took = steady_clock::now() - t0;
        RTF_ASSERT(took >= milliseconds(250) && took < seconds(2));
        // The queue itself goes away while its threads are still stuck.
    }

    // Those running finish on their own once let go, without the queue. The
    // one that never started is abandoned: its destructor never runs, so it
    // can't touch what revere has already freed.
    for(auto& g : gates)
        g->release();
    RTF_ASSERT(wait_until([&]() { lock_guard<mutex> l(rec->m); return rec->destroyed.size() == (size_t)r_teardown_queue::THREADS; }));
    this_thread::sleep_for(milliseconds(100));
    RTF_ASSERT(!destroyed(rec, "queued"));
}

void test_r_teardown_queue::test_push_after_finish()
{
    auto rec = make_shared<recorder>();
    r_teardown_queue q;
    RTF_ASSERT(q.finish(seconds(1)));
    q.push("late", "Late Cam", make("late", rec));
    // No thread to take it: destroyed here, now.
    lock_guard<mutex> l(rec->m);
    RTF_ASSERT(rec->destroyed == vector<string>({"late"}));
    RTF_ASSERT(rec->last_thread == this_thread::get_id());
    RTF_ASSERT(!q.pending("late"));
}
