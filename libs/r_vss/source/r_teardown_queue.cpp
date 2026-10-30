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
    string key;
    string name;
    shared_ptr<void> doomed;
};

}

// Everything the thread touches lives here, shared with it: a thread left
// running by finish() never reaches into a destroyed queue.
struct r_teardown_queue::state
{
    mutable mutex lock;
    condition_variable cv;        // work pushed, or finishing
    condition_variable done_cv;   // a teardown finished
    deque<item> items;
    bool busy {false};
    string current_key;
    string current_name;
    steady_clock::time_point current_since;
    bool finishing {false};
};

r_teardown_queue::r_teardown_queue() :
    _state(make_shared<state>())
{
    auto st = _state;
    _th = thread([st]() {
        while(true)
        {
            item it;
            {
                unique_lock<mutex> g(st->lock);
                st->cv.wait(g, [&]() { return !st->items.empty() || st->finishing; });
                if(st->items.empty())
                    return;
                it = std::move(st->items.front());
                st->items.pop_front();
                st->busy = true;
                st->current_key = it.key;
                st->current_name = it.name;
                st->current_since = steady_clock::now();
            }

            try
            {
                it.doomed.reset();
            }
            catch(const exception& e)
            {
                R_LOG_ERROR("teardown %s: %s", it.key.c_str(), e.what());
            }

            {
                lock_guard<mutex> g(st->lock);
                st->busy = false;
                st->current_key.clear();
                st->current_name.clear();
            }
            st->done_cv.notify_all();
        }
    });
}

r_teardown_queue::~r_teardown_queue() noexcept
{
    if(_th.joinable())
        finish(seconds(10));
}

void r_teardown_queue::push(const string& key, const string& name, shared_ptr<void> doomed)
{
    {
        lock_guard<mutex> g(_state->lock);
        if(_state->finishing)
            R_LOG_ERROR("teardown %s: pushed after finish; destroying it on the caller's thread", key.c_str());
        else
            _state->items.push_back({key, name, std::move(doomed)});
    }
    _state->cv.notify_all();
    // Refused: dropped here, outside the lock (a moved-from pointer is already empty).
    doomed.reset();
}

bool r_teardown_queue::pending(const string& key) const
{
    lock_guard<mutex> g(_state->lock);
    if(_state->busy && _state->current_key == key)
        return true;
    for(auto& it : _state->items)
        if(it.key == key)
            return true;
    return false;
}

vector<r_teardown_queue::stuck_item> r_teardown_queue::stuck(int64_t min_ms) const
{
    vector<stuck_item> out;
    lock_guard<mutex> g(_state->lock);
    if(_state->busy)
    {
        auto ms = duration_cast<milliseconds>(steady_clock::now() - _state->current_since).count();
        if(ms >= min_ms)
            out.push_back({_state->current_key, _state->current_name, (int64_t)ms});
    }
    return out;
}

bool r_teardown_queue::finish(milliseconds timeout)
{
    if(!_th.joinable())
        return true;

    bool done;
    {
        unique_lock<mutex> g(_state->lock);
        _state->finishing = true;
        _state->cv.notify_all();
        done = _state->done_cv.wait_for(g, timeout, [&]() { return _state->items.empty() && !_state->busy; });
        if(!done)
        {
            auto ms = duration_cast<milliseconds>(steady_clock::now() - _state->current_since).count();
            R_LOG_ERROR("teardown %s (%s) still hasn't finished after %lld ms; leaving it (%zu more queued) so revere can exit",
                        _state->current_key.c_str(), _state->current_name.c_str(), (long long)ms, _state->items.size());
        }
    }

    if(done)
        _th.join();
    else
        _th.detach();
    return done;
}
