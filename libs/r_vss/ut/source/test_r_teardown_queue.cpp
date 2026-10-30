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

void test_r_teardown_queue::test_destroys_in_order()
{
    auto rec = make_shared<recorder>();
    r_teardown_queue q;
    for(auto name : {"a", "b"})
    {
        auto obj = make_shared<doomed>();
        obj->name = name; obj->rec = rec;
        q.push(name, name, obj);
    }
    RTF_ASSERT(q.finish(seconds(2)));
    lock_guard<mutex> g(rec->m);
    // Each once, in the order pushed, and not on the caller's thread.
    RTF_ASSERT(rec->destroyed == vector<string>({"a", "b"}));
    RTF_ASSERT(rec->last_thread != this_thread::get_id());
    RTF_ASSERT(!q.pending("a") && !q.pending("b"));
}

void test_r_teardown_queue::test_pending_until_destroyed()
{
    auto rec = make_shared<recorder>();
    auto g = make_shared<gate>();
    r_teardown_queue q;
    auto obj = make_shared<doomed>();
    obj->name = "cam"; obj->rec = rec; obj->blocker = g;
    q.push("cam", "Cam", obj);
    obj.reset();

    RTF_ASSERT(wait_until([&]() { return g->entered.load(); }));
    // Being torn down: the keeper mustn't start the camera again yet.
    RTF_ASSERT(q.pending("cam"));
    RTF_ASSERT(!q.pending("other"));

    g->release();
    RTF_ASSERT(wait_until([&]() { return !q.pending("cam"); }));
    RTF_ASSERT(q.finish(seconds(2)));
}

void test_r_teardown_queue::test_stuck_teardown_doesnt_block_the_caller()
{
    auto rec = make_shared<recorder>();
    auto g = make_shared<gate>();
    r_teardown_queue q;
    {
        auto obj = make_shared<doomed>();
        obj->name = "stuck"; obj->rec = rec; obj->blocker = g;
        q.push("stuck", "Stuck Cam", obj);
    }
    RTF_ASSERT(wait_until([&]() { return g->entered.load(); }));

    // The caller (the keeper's thread) goes on: another push returns at once,
    // and the stuck one is reported with its name and how long.
    auto t0 = steady_clock::now();
    {
        auto obj = make_shared<doomed>();
        obj->name = "next"; obj->rec = rec;
        q.push("next", "Next Cam", obj);
    }
    RTF_ASSERT(steady_clock::now() - t0 < milliseconds(200));
    RTF_ASSERT(q.pending("next"));

    this_thread::sleep_for(milliseconds(60));
    auto s = q.stuck(50);
    RTF_ASSERT(s.size() == 1);
    RTF_ASSERT(s[0].key == "stuck");
    RTF_ASSERT(s[0].name == "Stuck Cam");
    RTF_ASSERT(s[0].ms >= 50);
    RTF_ASSERT(q.stuck(60000).empty());

    // Once it comes loose, the queue catches up.
    g->release();
    RTF_ASSERT(q.finish(seconds(2)));
    lock_guard<mutex> l(rec->m);
    RTF_ASSERT(rec->destroyed.size() == 2);
    RTF_ASSERT(rec->destroyed[0] == "stuck" && rec->destroyed[1] == "next");
}

void test_r_teardown_queue::test_finish_is_bounded()
{
    auto rec = make_shared<recorder>();
    auto g = make_shared<gate>();
    {
        r_teardown_queue q;
        {
            auto obj = make_shared<doomed>();
            obj->name = "stuck"; obj->rec = rec; obj->blocker = g;
            q.push("stuck", "Stuck Cam", obj);
        }
        RTF_ASSERT(wait_until([&]() { return g->entered.load(); }));

        // revere quitting: the wait ends at its bound, not when the teardown does.
        auto t0 = steady_clock::now();
        RTF_ASSERT(!q.finish(milliseconds(300)));
        auto took = steady_clock::now() - t0;
        RTF_ASSERT(took >= milliseconds(250) && took < seconds(2));
        // The queue itself goes away while its thread is still stuck.
    }

    // The thread left behind finishes on its own once let go, without the queue.
    g->release();
    RTF_ASSERT(wait_until([&]() { lock_guard<mutex> l(rec->m); return rec->destroyed.size() == 1; }));
}

void test_r_teardown_queue::test_push_after_finish()
{
    auto rec = make_shared<recorder>();
    r_teardown_queue q;
    RTF_ASSERT(q.finish(seconds(1)));
    {
        auto obj = make_shared<doomed>();
        obj->name = "late"; obj->rec = rec;
        q.push("late", "Late Cam", obj);
    }
    // No thread to take it: destroyed here, now.
    lock_guard<mutex> l(rec->m);
    RTF_ASSERT(rec->destroyed.size() == 1);
    RTF_ASSERT(rec->last_thread == this_thread::get_id());
    RTF_ASSERT(!q.pending("late"));
}
