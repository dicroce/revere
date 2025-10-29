
#ifndef r_utils_r_actor_h
#define r_utils_r_actor_h

#include <mutex>
#include <condition_variable>
#include <future>
#include <deque>
#include "r_utils/r_macro.h"
#include "r_utils/r_logger.h"

namespace r_utils
{

/// An object that inherits from r_actor is an entity with a thread that responds to commands
/// sent to it (in the order they were sent) producing some kind of result. r_actor is a template
/// class that parameterizes both the command and result types. In addition, r_actor returns a
/// std::future<> from post(), thus allowing clients the freedom to choose whether to block until
/// their command has a response or keep running until it does.

template<class CMD, class RESULT>
class r_actor
{
 public:
    R_API r_actor() = default;
    R_API r_actor(const r_actor&) = delete;
    R_API r_actor(r_actor&&) noexcept = delete;

    R_API virtual ~r_actor() noexcept
    {
        if(started())
            stop();
    }

    R_API r_actor& operator=(const r_actor&) = delete;
    R_API r_actor& operator=(r_actor&&) noexcept = delete;

    R_API void start()
    {
        std::unique_lock<std::mutex> g(_lock);
        if(started()) return;
        _started = true;
        _thread = std::thread(&r_actor<CMD,RESULT>::_main_loop, this);
    }

    R_API inline bool started() const
    {
        return _started;
    }

    R_API void stop()
    {
        if( started() )
        {
            {
                std::unique_lock<std::mutex> g(_lock);
                _started = false;
                _cond.notify_all();

                for (auto& [cmd, prom] : _queue)
                    prom.set_exception(std::make_exception_ptr(std::runtime_error("Actor stopped")));

                _queue.clear();
            }

            _thread.join();
        }
    }

    R_API std::future<RESULT> post(CMD cmd)
    {
        std::unique_lock<std::mutex> g(_lock);

        std::promise<RESULT> p;
        auto waiter = p.get_future();

        _queue.push_front(std::pair<CMD,std::promise<RESULT>>(cmd, std::move(p)));

        _cond.notify_one();

        return waiter;
    }

    R_API virtual RESULT process(const CMD& cmd) = 0;

protected:
    void _main_loop()
    {
        while(_started)
        {
            std::pair<CMD,std::promise<RESULT>> item;

            {
                std::unique_lock<std::mutex> g(_lock);

                _cond.wait(g, [this] () { return !this->_queue.empty() || !this->_started; });

                if(!_started)
                    continue;

                item = std::move(_queue.back());
                _queue.pop_back();
            }

            try
            {
                item.second.set_value(process(item.first));
            }
            catch(...)
            {
                try
                {
                    item.second.set_exception(std::current_exception());
                }
                catch(...)
                {
                    R_LOG_NOTICE("Failed to attach unknown exception to std::promise<>");
                }
            }
        }
    }

    std::thread _thread;
    std::mutex _lock;
    std::condition_variable _cond;
    std::deque<std::pair<CMD,std::promise<RESULT>>> _queue;
    std::atomic<bool> _started{false};
};

}

#endif
