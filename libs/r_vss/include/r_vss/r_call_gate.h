#ifndef __r_vss_r_call_gate_h
#define __r_vss_r_call_gate_h

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace r_vss
{

// Lets calls into a plugin be counted while they are inside it, so that stopping can wait for them. A call enters
// with enter() (false: the gate is closed, don't call) and leaves with leave(). close_and_wait() closes it to new
// calls and waits for those inside, but only up to a limit: a plugin that does not return must not hang the stop
// (and a plugin that calls back into the host from inside its call would wait for itself).
class r_call_gate final
{
public:
    r_call_gate() = default;
    r_call_gate(const r_call_gate&) = delete;
    r_call_gate& operator=(const r_call_gate&) = delete;

    bool enter()
    {
        std::lock_guard<std::mutex> g(_m);
        if(!_open)
            return false;
        ++_inside;
        return true;
    }

    void leave()
    {
        {
            std::lock_guard<std::mutex> g(_m);
            --_inside;
        }
        _cv.notify_all();
    }

    // False once close_and_wait() has closed it (until open()): a call that is already inside can ask whether it should
    // go on to the next plugin.
    bool is_open() const
    {
        std::lock_guard<std::mutex> g(_m);
        return _open;
    }

    void open()
    {
        std::lock_guard<std::mutex> g(_m);
        _open = true;
    }

    // Returns true when no call is inside any more, false when `limit` passed with some still inside.
    bool close_and_wait(std::chrono::milliseconds limit)
    {
        std::unique_lock<std::mutex> g(_m);
        _open = false;
        return _cv.wait_for(g, limit, [this]() { return _inside == 0; });
    }

private:
    mutable std::mutex _m;
    std::condition_variable _cv;
    int _inside {0};
    bool _open {true};
};

// Enters a gate for the scope; `entered()` says whether it was open.
class r_call_gate_scope final
{
public:
    explicit r_call_gate_scope(r_call_gate& gate) : _gate(gate), _entered(gate.enter()) {}
    r_call_gate_scope(const r_call_gate_scope&) = delete;
    r_call_gate_scope& operator=(const r_call_gate_scope&) = delete;
    ~r_call_gate_scope() { if(_entered) _gate.leave(); }
    bool entered() const { return _entered; }
private:
    r_call_gate& _gate;
    bool _entered;
};

}

#endif
