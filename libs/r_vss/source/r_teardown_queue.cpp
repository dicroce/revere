#include "r_vss/r_teardown_queue.h"
#include "r_utils/r_logger.h"
#include <condition_variable>
#include <deque>
#include <mutex>

using namespace r_vss;
using namespace std;
using namespace std::chrono;

namespace
{

struct item
{
    uint64_t id {0};
    string key;
    string name;
    shared_ptr<void> doomed;
    steady_clock::time_point since;   // pushed
    bool started {false};
};

}

// Everything the threads touch lives here, shared with them: a thread left
// running by finish() never reaches into a destroyed queue.
struct r_teardown_queue::state
{
    mutable mutex lock;
    condition_variable cv;        // work pushed, or finishing
    condition_variable done_cv;   // a teardown finished
    deque<item> items;            // queued and running, in push order
    bool finishing {false};
    bool abandoned {false};       // finish() gave up: start nothing more
    uint64_t next_id {1};
};

r_teardown_queue::r_teardown_queue() :
    _state(make_shared<state>())
{
    for(int i = 0; i < THREADS; ++i)
    {
        auto st = _state;
        _threads.emplace_back([st]() {
            while(true)
            {
                shared_ptr<void> doomed;
                string key;
                uint64_t id = 0;
                {
                    unique_lock<mutex> g(st->lock);
                    auto next = [&]() {
                        for(auto it = st->items.begin(); it != st->items.end(); ++it)
                            if(!it->started)
                                return it;
                        return st->items.end();
                    };
                    st->cv.wait(g, [&]() { return st->abandoned || st->finishing || next() != st->items.end(); });
                    if(st->abandoned)
                        return;
                    auto it = next();
                    if(it == st->items.end())
                        return;   // finishing, and nothing left to start
                    it->started = true;
                    key = it->key;
                    id = it->id;
                    // The queue's reference moves here; dropping it below destroys the object.
                    doomed = std::move(it->doomed);
                }

                try
                {
                    doomed.reset();
                }
                catch(const exception& e)
                {
                    R_LOG_ERROR("teardown %s: %s", key.c_str(), e.what());
                }

                {
                    lock_guard<mutex> g(st->lock);
                    for(auto it = st->items.begin(); it != st->items.end(); ++it)
                        if(it->id == id)
                        {
                            st->items.erase(it);
                            break;
                        }
                }
                st->done_cv.notify_all();
            }
        });
    }
}

r_teardown_queue::~r_teardown_queue() noexcept
{
    if(!_threads.empty())
        finish(seconds(10));
}

void r_teardown_queue::push(const string& key, const string& name, shared_ptr<void> doomed)
{
    {
        lock_guard<mutex> g(_state->lock);
        if(_state->finishing)
            R_LOG_ERROR("teardown %s: pushed after finish; destroying it on the caller's thread", key.c_str());
        else
            _state->items.push_back({_state->next_id++, key, name, std::move(doomed), steady_clock::now(), false});
    }
    _state->cv.notify_all();
    // Refused: dropped here, outside the lock (a moved-from pointer is already empty).
    doomed.reset();
}

bool r_teardown_queue::pending(const string& key) const
{
    lock_guard<mutex> g(_state->lock);
    for(auto& it : _state->items)
        if(it.key == key)
            return true;
    return false;
}

vector<r_teardown_queue::stuck_item> r_teardown_queue::stuck(int64_t min_ms) const
{
    vector<stuck_item> out;
    auto now = steady_clock::now();
    lock_guard<mutex> g(_state->lock);
    for(auto& it : _state->items)
    {
        auto ms = duration_cast<milliseconds>(now - it.since).count();
        if(ms >= min_ms)
            out.push_back({it.key, it.name, (int64_t)ms, it.started});
    }
    return out;
}

bool r_teardown_queue::finish(milliseconds timeout)
{
    if(_threads.empty())
        return true;

    bool done;
    {
        unique_lock<mutex> g(_state->lock);
        _state->finishing = true;
        _state->cv.notify_all();
        done = _state->done_cv.wait_for(g, timeout, [&]() { return _state->items.empty(); });
        if(!done)
        {
            // revere is exiting. What hasn't started never will: those objects
            // are leaked, not destroyed, since their destructors would run
            // against a stream keeper and a GStreamer that are going away. One
            // already running can't be stopped; its thread is left to it.
            _state->abandoned = true;
            auto now = steady_clock::now();
            for(auto& it : _state->items)
            {
                auto ms = (long long)duration_cast<milliseconds>(now - it.since).count();
                if(it.started)
                    R_LOG_ERROR("teardown %s (%s) still hasn't finished after %lld ms; leaving it so revere can exit",
                                it.key.c_str(), it.name.c_str(), ms);
                else
                {
                    R_LOG_ERROR("teardown %s (%s) never started (queued %lld ms); abandoned", it.key.c_str(), it.name.c_str(), ms);
                    new shared_ptr<void>(std::move(it.doomed));   // deliberately leaked
                }
            }
            _state->cv.notify_all();
        }
    }

    // The threads not stuck return now (done, or abandoned); stuck ones are detached.
    for(auto& th : _threads)
    {
        if(done)
            th.join();
        else
            th.detach();
    }
    _threads.clear();
    return done;
}
