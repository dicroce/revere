
#ifndef r_utils_r_work_q_h
#define r_utils_r_work_q_h

#include "r_utils/r_nullable.h"

#include <mutex>
#include <condition_variable>
#include <future>
#include <list>
#include <map>
#include <chrono>

namespace r_utils
{

template<typename CMD, typename RESULT>
class r_work_q final
{
public:
    R_API std::future<RESULT> post(const CMD& cmd)
    {
        std::unique_lock<std::mutex> g(_lock);

        std::promise<RESULT> p;
        auto waiter = p.get_future();

        _queue.push_front(make_pair(cmd, std::move(p)));

        _cond.notify_one();

        return std::move(waiter);
    }

    R_API r_utils::r_nullable<std::pair<CMD,std::promise<RESULT>>> poll(std::chrono::milliseconds d = {})
    {
        std::unique_lock<std::mutex> g(_lock);

        if(_queue.empty())
        {
            if(d == std::chrono::milliseconds {})
                _cond.wait(g, [this](){return !this->_queue.empty() || this->_woken;});
            else _cond.wait_for(g, d,[this](){return !this->_queue.empty() || this->_woken;});
        }

        r_utils::r_nullable<std::pair<CMD,std::promise<RESULT>>> result;

        if(!_queue.empty())
        {
            result.assign(std::move(_queue.back()));
            _queue.pop_back();
        }
        else
        {
            // Queue is still empty after wait - we were woken up, so clear the flag
            _woken = false;
        }

        return result;
    }

    R_API void wake()
    {
        std::unique_lock<std::mutex> g(_lock);
        _woken = true;
        _cond.notify_one();
    }

    R_API size_t size() const
    {
        std::unique_lock<std::mutex> g(_lock);
        return _queue.size();
    }

private:
    mutable std::mutex _lock;
    std::condition_variable _cond;
    std::list<std::pair<CMD,std::promise<RESULT>>> _queue;
    bool _woken {false};
};

}

#endif
