#include "test_r_call_gate.h"
#include "r_vss/r_call_gate.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace r_vss;
using namespace std;
using namespace std::chrono;

REGISTER_TEST_FIXTURE(test_r_call_gate);

void test_r_call_gate::test_closing_with_nothing_inside_returns_at_once()
{
    r_call_gate g;
    auto start = steady_clock::now();
    RTF_ASSERT(g.close_and_wait(seconds(5)));
    RTF_ASSERT(steady_clock::now() - start < seconds(1));
}

void test_r_call_gate::test_a_closed_gate_lets_no_one_in()
{
    r_call_gate g;
    g.close_and_wait(milliseconds(10));
    r_call_gate_scope s(g);
    RTF_ASSERT(!s.entered());
}

void test_r_call_gate::test_closing_waits_for_a_call_inside()
{
    r_call_gate g;
    atomic<bool> left(false);
    atomic<bool> inside(false);
    thread t([&]() {
        r_call_gate_scope s(g);
        inside = true;
        this_thread::sleep_for(milliseconds(100));
        left = true;
    });
    while(!inside)
        this_thread::sleep_for(milliseconds(1));
    const bool closed = g.close_and_wait(seconds(5));
    const bool had_left = left.load();
    t.join();
    RTF_ASSERT(closed);
    RTF_ASSERT(had_left);   // the call had returned
}

void test_r_call_gate::test_closing_gives_up_on_a_call_that_does_not_return()
{
    r_call_gate g;
    atomic<bool> inside(false);
    atomic<bool> release(false);
    thread t([&]() {
        r_call_gate_scope s(g);
        inside = true;
        while(!release)
            this_thread::sleep_for(milliseconds(1));
    });
    while(!inside)
        this_thread::sleep_for(milliseconds(1));
    auto start = steady_clock::now();
    const bool closed = g.close_and_wait(milliseconds(100));
    auto took = steady_clock::now() - start;
    // Let the thread go and join it before judging: a failed assert leaves by an exception, and a joinable thread
    // destroyed on the way out would end the whole run in std::terminate.
    release = true;
    t.join();
    RTF_ASSERT(!closed);
    RTF_ASSERT(took >= milliseconds(90));
    RTF_ASSERT(took < seconds(2));
}

void test_r_call_gate::test_opening_lets_calls_in_again()
{
    r_call_gate g;
    g.close_and_wait(milliseconds(10));
    g.open();
    r_call_gate_scope s(g);
    RTF_ASSERT(s.entered());
}

void test_r_call_gate::test_is_open_follows_closing_and_opening()
{
    r_call_gate g;
    const bool at_first = g.is_open();
    g.close_and_wait(milliseconds(10));
    const bool closed = !g.is_open();
    g.open();
    const bool again = g.is_open();
    RTF_ASSERT(at_first);
    RTF_ASSERT(closed);
    RTF_ASSERT(again);
}
