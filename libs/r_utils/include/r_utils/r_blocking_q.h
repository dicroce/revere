
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

// Drop policy when queue is full
enum class r_queue_full_policy
{
    drop_newest,  // Don't add new item (default)
    drop_oldest   // Remove oldest item to make room
};

template<typename DATA>
class r_blocking_q final
{
public:
    // Default constructor - unbounded queue (backward compatible)
    R_API r_blocking_q() : _max_size(0) {}

    // Constructor with max size limit
    R_API explicit r_blocking_q(size_t max_size, r_queue_full_policy policy = r_queue_full_policy::drop_oldest)
        : _max_size(max_size), _policy(policy) {}

    // Returns true if item was added, false if dropped due to queue full
    R_API bool post(const DATA& d)
    {
        std::unique_lock<std::mutex> g(_lock);

        if(_max_size > 0 && _queue.size() >= _max_size)
        {
            if(_policy == r_queue_full_policy::drop_oldest)
            {
                _queue.pop_front();
                ++_dropped_count;
            }
            else
            {
                ++_dropped_count;
                return false;
            }
        }

        _queue.push_back(d);

        _cond.notify_one();
        return true;
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

    R_API size_t dropped_count() const
    {
        std::unique_lock<std::mutex> g(_lock);
        return _dropped_count;
    }

    R_API void reset_dropped_count()
    {
        std::unique_lock<std::mutex> g(_lock);
        _dropped_count = 0;
    }

    R_API size_t max_size() const
    {
        return _max_size;
    }

private:
    mutable std::mutex _lock;
    std::condition_variable _cond;
    std::deque<DATA> _queue;
    bool _asleep {false};
    size_t _max_size {0};  // 0 means unbounded
    r_queue_full_policy _policy {r_queue_full_policy::drop_oldest};
    size_t _dropped_count {0};
};

}

#endif
