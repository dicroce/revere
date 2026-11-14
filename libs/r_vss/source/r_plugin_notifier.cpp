#include "r_vss/r_plugin_notifier.h"
#include "r_utils/r_logger.h"

using namespace r_vss;
using namespace std;

r_plugin_notifier::r_plugin_notifier(size_t max_pending) :
    _max_pending(max_pending > 0 ? max_pending : 1)
{
    _th = thread(&r_plugin_notifier::_run, this);
}

r_plugin_notifier::~r_plugin_notifier()
{
    {
        lock_guard<mutex> g(_m);
        _stop = true;
        _q.clear();
    }
    _cv.notify_all();
    if(_th.joinable())
        _th.join();
}

void r_plugin_notifier::accept(bool on)
{
    unique_lock<mutex> g(_m);
    _accepting = on;
    if(on)
        return;
    _q.clear();
    // The call that is running is let finish, so none is still inside the plugin when this returns. (Not when this is
    // that call itself, which would wait for itself.)
    if(this_thread::get_id() != _th.get_id())
        _idle.wait(g, [this]() { return !_running; });
}

void r_plugin_notifier::post(function<void()> call)
{
    {
        lock_guard<mutex> g(_m);
        if(!_accepting || _stop)
            return;
        if(_q.size() >= _max_pending)
        {
            _q.pop_front();
            ++_dropped;
        }
        _q.push_back(std::move(call));
    }
    _cv.notify_one();
}

size_t r_plugin_notifier::dropped() const
{
    lock_guard<mutex> g(_m);
    return _dropped;
}

void r_plugin_notifier::_run()
{
    while(true)
    {
        function<void()> call;
        {
            unique_lock<mutex> g(_m);
            _cv.wait(g, [this]() { return _stop || !_q.empty(); });
            if(_stop)
                return;
            call = std::move(_q.front());
            _q.pop_front();
            _running = true;
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
            lock_guard<mutex> g(_m);
            _running = false;
        }
        _idle.notify_all();
    }
}
