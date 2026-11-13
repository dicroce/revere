#include "test_r_plugin_notifier.h"
#include "r_vss/r_plugin_notifier.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace r_vss;
using namespace std;
using namespace std::chrono;

REGISTER_TEST_FIXTURE(test_r_plugin_notifier);

namespace
{

// Waits until pred holds, for at most 5 s.
template<class F>
bool eventually(F pred)
{
    auto end = steady_clock::now() + seconds(5);
    while(steady_clock::now() < end)
    {
        if(pred())
            return true;
        this_thread::sleep_for(milliseconds(5));
    }
    return pred();
}

struct log_of
{
    mutex m;
    vector<int> v;
    void add(int i) { lock_guard<mutex> g(m); v.push_back(i); }
    vector<int> get() { lock_guard<mutex> g(m); return v; }
    size_t size() { lock_guard<mutex> g(m); return v.size(); }
};

// A call that waits at a gate until it is opened.
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

}

void test_r_plugin_notifier::test_runs_calls_in_order()
{
    auto l = make_shared<log_of>();
    r_plugin_notifier n;
    n.accept(true);
    for(int i = 0; i < 50; ++i)
        n.post([l, i]() { l->add(i); });
    RTF_ASSERT(eventually([&]() { return l->size() == 50; }));
    auto v = l->get();
    for(int i = 0; i < 50; ++i)
        RTF_ASSERT_EQUAL(v[i], i);
}

void test_r_plugin_notifier::test_nothing_until_accepting()
{
    auto l = make_shared<log_of>();
    r_plugin_notifier n;
    n.post([l]() { l->add(1); });          // not accepting yet
    n.accept(true);
    n.post([l]() { l->add(2); });
    RTF_ASSERT(eventually([&]() { return l->size() >= 1; }));
    this_thread::sleep_for(milliseconds(50));
    auto v = l->get();
    RTF_ASSERT_EQUAL(v.size(), (size_t)1);
    RTF_ASSERT_EQUAL(v[0], 2);
}

void test_r_plugin_notifier::test_a_slow_call_does_not_hold_up_post()
{
    auto g = make_shared<gate>();
    r_plugin_notifier n;
    n.accept(true);
    n.post([g]() { g->wait(); });
    RTF_ASSERT(eventually([&]() { return g->entered.load(); }));

    auto start = steady_clock::now();
    for(int i = 0; i < 100; ++i)
        n.post([]() {});
    // 100 posts behind a call that hasn't returned: none of them waited for it.
    RTF_ASSERT(steady_clock::now() - start < seconds(1));
    g->release();
}

void test_r_plugin_notifier::test_overflow_drops_the_oldest()
{
    auto g = make_shared<gate>();
    auto l = make_shared<log_of>();
    r_plugin_notifier n(3);
    n.accept(true);
    n.post([g]() { g->wait(); });
    RTF_ASSERT(eventually([&]() { return g->entered.load(); }));

    for(int i = 1; i <= 5; ++i)
        n.post([l, i]() { l->add(i); });   // room for 3: 1 and 2 go
    RTF_ASSERT_EQUAL(n.dropped(), (size_t)2);
    g->release();
    RTF_ASSERT(eventually([&]() { return l->size() == 3; }));
    auto v = l->get();
    RTF_ASSERT_EQUAL(v[0], 3);
    RTF_ASSERT_EQUAL(v[1], 4);
    RTF_ASSERT_EQUAL(v[2], 5);
}

void test_r_plugin_notifier::test_a_throwing_call_does_not_stop_the_rest()
{
    auto l = make_shared<log_of>();
    r_plugin_notifier n;
    n.accept(true);
    n.post([]() { throw runtime_error("a plugin's call failed"); });
    n.post([l]() { l->add(7); });
    RTF_ASSERT(eventually([&]() { return l->size() == 1; }));
}

void test_r_plugin_notifier::test_stopping_accepting_drops_what_is_queued()
{
    auto g = make_shared<gate>();
    auto l = make_shared<log_of>();
    r_plugin_notifier n;
    n.accept(true);
    n.post([g]() { g->wait(); });
    RTF_ASSERT(eventually([&]() { return g->entered.load(); }));
    n.post([l]() { l->add(1); });
    n.accept(false);
    n.post([l]() { l->add(2); });
    g->release();
    this_thread::sleep_for(milliseconds(100));
    RTF_ASSERT_EQUAL(l->size(), (size_t)0);
}

void test_r_plugin_notifier::test_destructor_waits_only_for_the_running_call()
{
    auto g = make_shared<gate>();
    auto l = make_shared<log_of>();
    atomic<bool> destroyed(false);
    thread t([&]() {
        {
            r_plugin_notifier n;
            n.accept(true);
            n.post([g]() { g->wait(); });
            while(!g->entered)
                this_thread::sleep_for(milliseconds(1));
            for(int i = 0; i < 10; ++i)
                n.post([l, i]() { l->add(i); });
        }
        destroyed = true;
    });
    this_thread::sleep_for(milliseconds(100));
    RTF_ASSERT(!destroyed);   // the running call is waited for
    g->release();
    t.join();
    RTF_ASSERT(destroyed);
    RTF_ASSERT_EQUAL(l->size(), (size_t)0);   // and the queued ones are not run
}
