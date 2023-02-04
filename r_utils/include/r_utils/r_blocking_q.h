
#ifndef r_utils_r_blocking_q_h
#define r_utils_r_blocking_q_h

#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"

#include <mutex>
#include <condition_variable>
#include <future>
#include <deque>
#include <map>
#include <chrono>

namespace r_utils
{

template<typename DATA>
class r_blocking_q final
{
public:
    R_API void post(const DATA& d)
    {
        std::unique_lock<std::mutex> g(_lock);

        _queue.push_back(d);

        _cond.notify_one();
    }

    R_API r_utils::r_nullable<DATA> poll(std::chrono::milliseconds d = {})
    {
        std::unique_lock<std::mutex> g(_lock);

        if(_queue.empty())
        {
            _asleep = true;
            if(d == std::chrono::milliseconds {})
                _cond.wait(g, [this](){return !this->_queue.empty() || !this->_asleep;});
            else _cond.wait_for(g, d, [this](){return !this->_queue.empty() || !this->_asleep;});
        }

        r_utils::r_nullable<DATA> result;

        if(!_queue.empty())
        {
            result.assign(std::move(_queue.front()));
            _queue.pop_front();
        }

        return result;
    }

    R_API void wake()
    {
        std::unique_lock<std::mutex> g(_lock);
        _asleep = false;
        _cond.notify_one();
    }

    R_API void clear()
    {
        std::unique_lock<std::mutex> g(_lock);
        _queue.clear();
        _asleep = false;
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
    std::deque<DATA> _queue;
    bool _asleep {false};
};

}

#endif
