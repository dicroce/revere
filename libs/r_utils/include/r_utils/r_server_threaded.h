
#ifndef r_http_r_server_threaded_h
#define r_http_r_server_threaded_h

#include "r_utils/r_socket.h"
#include "r_utils/r_socket_address.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_logger.h"

#include <mutex>
#include <thread>
#include <functional>
#include <list>
#include <future>

namespace r_utils
{

template<class SOK_T>
class r_server_threaded
{
private:
    struct conn_context
    {
        bool done {false};
        std::chrono::steady_clock::time_point doneTP;
        SOK_T connected;
        std::thread th;
    };

public:
    R_API r_server_threaded( int port, std::function<void(SOK_T& conn)> connCB, const std::string& sockAddr = std::string() ) :
        _serverSocket(),
        _port( port ),
        _connCB( connCB ),
        _sockAddr( sockAddr ),
        _connectedContexts(),
        _running( false )
    {
    }

    R_API r_server_threaded(const r_server_threaded&) = delete;
    R_API r_server_threaded(r_server_threaded&&) = delete;

    R_API virtual ~r_server_threaded() noexcept
    {
        stop();

        for( const auto& c : _connectedContexts )
        {
            c->done = true;
            c->connected.close();
            c->th.join();
        }
    }

    R_API r_server_threaded& operator=(const r_server_threaded&) = delete;
    R_API r_server_threaded& operator=(r_server_threaded&&) = delete;

    R_API void stop()
    {
        if( _running )
        {
            _running = false;

            FULL_MEM_BARRIER();

            SOK_T sok;
            sok.connect( (_sockAddr.empty() || _sockAddr == r_utils::ip4_addr_any) ? "127.0.0.1" : _sockAddr, _port );
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

        _running = true;

        while( _running )
        {
            try
            {
                auto cc = std::make_shared<struct conn_context>();

                cc->connected = _serverSocket.accept();

                if( !_running )
                    continue;

                _connectedContexts.remove_if( []( const std::shared_ptr<struct conn_context>& context )->bool {
                    if( context->done && ((std::chrono::steady_clock::now() - context->doneTP) > std::chrono::seconds(30)) )
                    {
                        context->th.join();
                        return true;
                    }
                    return false;
                });
                cc->done = false;

                FULL_MEM_BARRIER();

                cc->th = std::thread( &r_server_threaded::_thread_start, this, cc );

                _connectedContexts.push_back( cc );
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

    R_API SOK_T& get_socket() { return _serverSocket; }

private:
    void _configure_server_socket()
    {
        if( _sockAddr.empty() )
            _serverSocket.bind( _port );
        else _serverSocket.bind( _port, _sockAddr );

        _serverSocket.listen();
    }

    void _thread_start( std::shared_ptr<struct conn_context> cc )
    {
        try
        {
            _connCB( cc->connected );
        }
        catch(std::exception& ex)
        {
            R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
        }
        catch(...)
        {
            R_LOG_ERROR("Unknown exception while responding to request.");
        }

        cc->doneTP = std::chrono::steady_clock::now();
        FULL_MEM_BARRIER();
        cc->done = true;
    }

    SOK_T _serverSocket;
    int _port;
    std::function<void(SOK_T& conn)> _connCB;
    std::string _sockAddr;
    std::list<std::shared_ptr<struct conn_context>> _connectedContexts;
    bool _running;
};

}

#endif
