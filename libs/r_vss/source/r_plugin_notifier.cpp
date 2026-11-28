#include "r_vss/r_plugin_notifier.h"
#include "r_utils/r_logger.h"

using namespace r_vss;
using namespace std;
using namespace std::chrono;

r_plugin_notifier::r_plugin_notifier(size_t max_pending) :
    _s(make_shared<_state>())
{
    _s->max_pending = max_pending > 0 ? max_pending : 1;
    _th = thread(&r_plugin_notifier::_run, _s);
}

r_plugin_notifier::~r_plugin_notifier()
{
    if(!shutdown(milliseconds(5000)))
        R_LOG_ERROR("A system plugin's notification thread did not return from a call; leaving it behind.");
}

bool r_plugin_notifier::shutdown(milliseconds limit)
{
    {
        lock_guard<mutex> g(_s->m);
        _s->stop = true;
        _s->accepting = false;
        _s->q.clear();
    }
    _s->cv.notify_all();
    if(!_th.joinable())
        return true;
    // Not for the call that is shutting its own notifier down: it could never be waited for.
    const bool from_inside = this_thread::get_id() == _th.get_id();
    bool done = false;
    if(!from_inside)
    {
        unique_lock<mutex> g(_s->m);
        done = _s->idle.wait_for(g, limit, [this]() { return _s->done; });
    }
    if(done)
        _th.join();
    else
        _th.detach();
    return done;
}

bool r_plugin_notifier::running() const
{
    lock_guard<mutex> g(_s->m);
    return _s->running;
}

bool r_plugin_notifier::accept(bool on, milliseconds limit)
{
    unique_lock<mutex> g(_s->m);
    _s->accepting = on;
    if(on)
        return true;
    _s->q.clear();
    // The call that is running is let finish, so none is still inside the plugin when this returns. (Not when this is
    // that call itself, which would wait for itself.)
    if(_th.joinable() && this_thread::get_id() == _th.get_id())
        return true;
    return _s->idle.wait_for(g, limit, [this]() { return !_s->running; });
}

void r_plugin_notifier::post(function<void()> call)
{
    {
        lock_guard<mutex> g(_s->m);
        if(!_s->accepting || _s->stop)
            return;
        if(_s->q.size() >= _s->max_pending)
        {
            _s->q.pop_front();
            ++_s->dropped;
        }
        _s->q.push_back(std::move(call));
    }
    _s->cv.notify_one();
}

size_t r_plugin_notifier::dropped() const
{
    lock_guard<mutex> g(_s->m);
    return _s->dropped;
}

void r_plugin_notifier::_run(shared_ptr<_state> s)
{
    while(true)
    {
        function<void()> call;
        {
            unique_lock<mutex> g(s->m);
            s->cv.wait(g, [&s]() { return s->stop || !s->q.empty(); });
            if(s->stop)
                break;
            call = std::move(s->q.front());
            s->q.pop_front();
            s->running = true;
        }
        try
        {
            call();
        }
        catch(const exception& e)
        {
            R_LOG_ERROR("A system plugin's notification failed: %s", e.what());
        }
        catch(...)
        {
            R_LOG_ERROR("A system plugin's notification failed.");
        }
        {
            lock_guard<mutex> g(s->m);
            s->running = false;
        }
        s->idle.notify_all();
    }
    {
        lock_guard<mutex> g(s->m);
        s->done = true;
    }
    s->idle.notify_all();
}
