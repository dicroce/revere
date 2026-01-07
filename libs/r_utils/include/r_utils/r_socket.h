
#ifndef r_utils_r_socket_h
#define r_utils_r_socket_h

#include "r_utils/interfaces/r_socket_base.h"
#include "r_utils/r_socket_address.h"
#include "r_utils/r_exception.h"

#ifdef IS_WINDOWS

    #include <WinSock2.h>
    #include <ws2tcpip.h>
    #include <Iphlpapi.h>

    using sock_t = SOCKET;
    static constexpr sock_t kInvalidSock = INVALID_SOCKET;

    #define SOCKET_SHUT_FLAGS SD_BOTH
    #define SOCKET_SHUT_SEND_FLAGS SD_SEND
    #define SOCKET_SHUT_RECV_FLAGS SD_RECEIVE

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#else

    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <arpa/inet.h>

    using sock_t = int;
    static constexpr sock_t kInvalidSock = -1;

    #define SOCKET_SHUT_FLAGS SHUT_RDWR
    #define SOCKET_SHUT_SEND_FLAGS SHUT_WR
    #define SOCKET_SHUT_RECV_FLAGS SHUT_RD

#endif

#include <memory>
#include <map>
#include <mutex>
#include <vector>
#include <algorithm>
#include <atomic>
#include <string.h>
#include <sys/types.h>

class test_r_utils;

namespace r_utils
{

/// The socket ID returned by get_sok_id() is safe to use for the duration of
/// operations on this object, but callers should not cache the ID across
/// potential close() calls from other threads.
class r_raw_socket : public r_socket_base
{
    friend class ::test_r_utils;

public:
    enum r_raw_socket_defaults
    {
        MAX_BACKLOG = 5
    };

    R_API static void socket_startup();
    R_API static void socket_cleanup();

    R_API r_raw_socket();
    R_API r_raw_socket( r_raw_socket&& obj ) noexcept;
    R_API r_raw_socket( const r_raw_socket& ) = delete;
    R_API virtual ~r_raw_socket() noexcept;

    R_API r_raw_socket& operator = ( r_raw_socket&& obj ) noexcept;
    R_API r_raw_socket& operator = ( const r_raw_socket& ) = delete;

    R_API void create( int af );

    R_API virtual void connect( const std::string& host, int port );
    R_API void listen( int backlog = MAX_BACKLOG );
    R_API void bind( int port, const std::string& ip = "" );
    R_API r_raw_socket accept();

    /// Returns the underlying socket ID.
    /// Thread-safe: protected by instance mutex.
    R_API sock_t get_sok_id() const;

    /// Returns true if the socket is valid (connected/bound).
    /// Thread-safe: protected by instance mutex.
    R_API virtual bool valid() const;

    R_API virtual int send(const void* buf, size_t len);
    R_API virtual int recv(void* buf, size_t len);

    R_API virtual void close();

    R_API virtual bool wait_till_recv_wont_block( uint64_t& millis ) const;
    R_API virtual bool wait_till_send_wont_block( uint64_t& millis ) const;

    R_API std::string get_peer_ip() const;
    R_API std::string get_local_ip() const;

protected:
    std::atomic<sock_t> _sok;
    r_socket_address _addr;
    std::string _host;

    static bool _sokSysStarted;
    static std::recursive_mutex _sokLock;  // Global lock for socket system startup/cleanup
};

class r_socket : public r_socket_base
{
    friend class ::test_r_utils;

public:
    enum r_socket_defaults
    {
        MAX_BACKLOG = 5
    };

    R_API inline r_socket() : _sok(), _ioTimeOut(5000) {}
    R_API inline r_socket( r_socket&& obj ) noexcept :
        _sok( std::move(obj._sok) ),
        _ioTimeOut( std::move(obj._ioTimeOut ))
    {
    }

    R_API r_socket( const r_socket& ) = delete;
    R_API inline virtual ~r_socket() noexcept {}

    R_API inline r_socket& operator = ( r_socket&& obj ) noexcept
    {
        if(valid())
            close();

        _sok = std::move(obj._sok);
        _ioTimeOut = std::move(obj._ioTimeOut);
        return *this;
    }

    R_API r_socket& operator = ( const r_socket& ) = delete;

    R_API void set_io_timeout( uint64_t ioTimeOut ) { _ioTimeOut = ioTimeOut; }

    R_API inline void create( int af ) { _sok.create(af); }

    R_API virtual void connect( const std::string& host, int port );
    R_API inline void listen( int backlog = MAX_BACKLOG ) { _sok.listen(backlog); }
    R_API inline void bind( int port, const std::string& ip = "" ) { 
        _sok.bind(port, ip ); 
    }
    R_API inline r_socket accept() { auto r = _sok.accept(); r_socket s; s._sok = std::move(r); return s; }

    R_API inline sock_t get_sok_id() const { return _sok.get_sok_id(); }

    R_API inline virtual bool valid() const { return _sok.valid(); }

    R_API virtual int send( const void* buf, size_t len );

    R_API virtual int recv( void* buf, size_t len );

    R_API inline void close() { _sok.close(); }

    R_API inline virtual bool wait_till_recv_wont_block( uint64_t& millis ) const { return _sok.wait_till_recv_wont_block(millis); }
    R_API inline virtual bool wait_till_send_wont_block( uint64_t& millis ) const { return _sok.wait_till_send_wont_block(millis); }

    R_API inline std::string get_peer_ip() const { return _sok.get_peer_ip(); }
    R_API inline std::string get_local_ip() const { return _sok.get_local_ip(); }

private:
    r_raw_socket _sok;
    uint64_t _ioTimeOut;
};

class r_socket_connect_exception : public r_exception
{
public:
    R_API r_socket_connect_exception(const char* msg, ...);
    R_API virtual ~r_socket_connect_exception() noexcept {}
};

class r_socket_exception : public r_exception
{
public:
    R_API r_socket_exception(const char* msg, ...);
    R_API virtual ~r_socket_exception() noexcept {}
};

namespace r_networking
{

R_API int r_send(r_socket_base& sok, const void* buf, size_t len, uint64_t millis = 5000);

R_API int r_recv(r_socket_base& sok, void* buf, size_t len, uint64_t millis = 5000);

R_API std::vector<std::string> r_resolve( int type, const std::string& name );

R_API std::vector<uint8_t> r_get_hardware_address(const std::string& ifname);

R_API std::string r_get_device_uuid(const std::string& ifname);

R_API uint16_t r_ntohs(uint16_t x);

R_API uint16_t r_htons(uint16_t x);

R_API uint32_t r_ntohl(uint32_t x);

R_API uint32_t r_htonl(uint32_t x);

R_API uint64_t r_ntohll(uint64_t x);

R_API uint64_t r_htonll(uint64_t x);

struct r_adapter_info
{
    std::string name;        // interface name (e.g., "eth0", "Ethernet")
    std::string ipv4_addr;   // IPv4 address as string
};

// Returns network adapters that are: UP, not loopback, multicast capable, and have an IPv4 address
R_API std::vector<r_adapter_info> r_get_adapters();

}

}

#endif
