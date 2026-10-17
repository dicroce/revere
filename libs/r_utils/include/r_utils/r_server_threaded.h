
#ifndef r_http_r_server_threaded_h
#define r_http_r_server_threaded_h

#include "r_utils/r_socket.h"
#include "r_utils/r_socket_address.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_logger.h"

#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <deque>
#include <list>
#include <memory>
#include <vector>
#include <chrono>
#include <atomic>

namespace r_utils
{

// Accepts connections on one thread and hands each to connCB on a fixed pool
// of worker threads. Accepted connections wait in a bounded queue for a free
// worker; when the queue is full a new connection is handed to the overload
// callback (if any) and closed, so a burst of slow requests can't pile up an
// unbounded number of threads.
template<class SOK_T>
class r_server_threaded
{
public:
    enum r_server_threaded_defaults
    {
        DEFAULT_WORKERS = 16,
        DEFAULT_MAX_PENDING = 64,
        LISTEN_BACKLOG = 128
    };

    R_API r_server_threaded(
        int port,
        std::function<void(SOK_T& conn)> connCB,
        const std::string& sockAddr = std::string(),
        size_t workers = DEFAULT_WORKERS,
        size_t maxPending = DEFAULT_MAX_PENDING
    ) :
        _serverSocket(),
        _port( port ),
        _connCB( connCB ),
        _sockAddr( sockAddr ),
        _nWorkers( workers > 0 ? workers : 1 ),
        _maxPending( maxPending ),
        _running( false )
    {
    }

    R_API r_server_threaded(const r_server_threaded&) = delete;
    R_API r_server_threaded(r_server_threaded&&) = delete;

    R_API virtual ~r_server_threaded() noexcept
    {
        stop();
        join_all();
    }

    R_API r_server_threaded& operator=(const r_server_threaded&) = delete;
    R_API r_server_threaded& operator=(r_server_threaded&&) = delete;

    // Called on the accept thread with a connection that won't be served
    // because every worker is busy and the queue is full (e.g. to write a
    // "503 Service Unavailable"); the connection is closed afterwards. Keep it
    // short: the accept loop waits for it. Set before start().
    R_API void set_overload_cb( std::function<void(SOK_T& conn)> overloadCB ) { _overloadCB = overloadCB; }

    // Stop the workers and wait for them. Queued connections that no worker
    // has picked up are closed unanswered; in-flight connections are closed to
    // unblock a handler parked on socket I/O, and a handler busy in compute
    // (e.g. a transcode decode) is allowed to run to completion. MUST be
    // called only after the accept loop (start()) has exited.
    // r_web_server::stop() enforces that ordering. Idempotent.
    R_API void join_all() noexcept
    {
        {
            std::lock_guard<std::mutex> g(_lock);
            _shutdown = true;
            for( auto& c : _pending )
            {
                try { c.close(); } catch(...) {}
            }
            _pending.clear();
            for( auto& c : _inFlight )
            {
                try { c->close(); } catch(...) {}
            }
        }
        _cond.notify_all();

        for( auto& w : _workers )
        {
            if( w.joinable() )
                w.join();
        }
        _workers.clear();
    }

    // Ends the accept loop: start() returns soon after. Safe to call before
    // start(), while it's starting, and more than once; never throws.
    R_API void stop() noexcept
    {
        _stopRequested = true;
        _running = false;

        // Wake an accept() that's waiting. Only once the socket is listening:
        // before that, start() sees _stopRequested itself and doesn't wait.
        if( _listening )
        {
            try
            {
                SOK_T sok;
                sok.connect( (_sockAddr.empty() || _sockAddr == r_utils::ip4_addr_any) ? "127.0.0.1" : _sockAddr, _port );
            }
            catch(...) {}
        }
    }

    R_API void start()
    {
        try
        {
            _configure_server_socket();
        }
        catch( std::exception& ex )
        {
            R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
            return;
        }
        catch( ... )
        {
            R_LOG_NOTICE("Unknown exception caught while initializing r_server_threaded. Exiting.");
            return;
        }

        _listening = true;

        // stop() may have run before the socket was listening, in which case
        // it couldn't wake accept(): check now that it would.
        if( _stopRequested )
            return;

        {
            std::lock_guard<std::mutex> g(_lock);
            _shutdown = false;
        }
        for( size_t i = 0; i < _nWorkers; ++i )
            _workers.emplace_back( &r_server_threaded::_worker, this );

        _running = true;

        while( !_stopRequested )
        {
            try
            {
                auto conn = _serverSocket.accept();

                if( _stopRequested )
                    continue;

                bool queued = false;
                {
                    std::lock_guard<std::mutex> g(_lock);
                    if( _pending.size() < _maxPending )
                    {
                        _pending.push_back( std::move(conn) );
                        queued = true;
                    }
                }

                if( queued )
                    _cond.notify_one();
                else _overloaded( conn );
            }
            catch( std::exception& ex )
            {
                R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
            }
            catch( ... )
            {
                R_LOG_NOTICE("An unknown exception has occurred while responding to connection.");
            }
        }
    }

    R_API bool started() const { return _running; }

    // True once stop() has begun (or the server never started). A request
    // handler that loops for a while can poll this each iteration and bail
    // early instead of making shutdown wait for it in join_all(). This is a
    // cooperative latency optimization, not a correctness mechanism — join_all()
    // still drains any handler that does not poll.
    R_API bool stopping() const { return _stopRequested.load() || !_running.load(); }

    R_API SOK_T& get_socket() { return _serverSocket; }

private:
    void _configure_server_socket()
    {
        if( _sockAddr.empty() )
            _serverSocket.bind( _port );
        else _serverSocket.bind( _port, _sockAddr );

        _serverSocket.listen( LISTEN_BACKLOG );
    }

    void _worker()
    {
        while( true )
        {
            std::shared_ptr<SOK_T> conn;
            {
                std::unique_lock<std::mutex> g(_lock);
                _cond.wait( g, [this](){ return _shutdown || !_pending.empty(); } );
                if( _shutdown )
                    return;
                conn = std::make_shared<SOK_T>( std::move(_pending.front()) );
                _pending.pop_front();
                _inFlight.push_back( conn );
            }

            try
            {
                _connCB( *conn );
            }
            catch(std::exception& ex)
            {
                R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
            }
            catch(...)
            {
                R_LOG_ERROR("Unknown exception while responding to request.");
            }

            std::lock_guard<std::mutex> g(_lock);
            _inFlight.remove( conn );
        }
    }

    void _overloaded( SOK_T& conn )
    {
        if( _overloadCB )
        {
            try { _overloadCB( conn ); } catch(...) {}
        }
        try { conn.close(); } catch(...) {}

        // Log the first refusal, then at most one summary every 10 seconds.
        ++_refused;
        auto now = std::chrono::steady_clock::now();
        if( _lastRefusedLog == std::chrono::steady_clock::time_point() || now - _lastRefusedLog >= std::chrono::seconds(10) )
        {
            R_LOG_WARNING("server on port %d busy: %zu connection(s) refused (%zu workers busy, %zu queued)",
                          _port, _refused, _nWorkers, _maxPending);
            _lastRefusedLog = now;
            _refused = 0;
        }
    }

    SOK_T _serverSocket;
    int _port;
    std::function<void(SOK_T& conn)> _connCB;
    std::function<void(SOK_T& conn)> _overloadCB;
    std::string _sockAddr;
    size_t _nWorkers;
    size_t _maxPending;
    std::mutex _lock;
    std::condition_variable _cond;
    std::deque<SOK_T> _pending;
    std::list<std::shared_ptr<SOK_T>> _inFlight;
    std::vector<std::thread> _workers;
    bool _shutdown {false};
    size_t _refused {0};
    std::chrono::steady_clock::time_point _lastRefusedLog;
    std::atomic<bool> _running;
    std::atomic<bool> _stopRequested {false};
    std::atomic<bool> _listening {false};
};

}

#endif
