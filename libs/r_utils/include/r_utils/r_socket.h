
#ifndef r_utils_r_socket_h
#define r_utils_r_socket_h

#include "r_utils/interfaces/r_socket_base.h"
#include "r_utils/r_socket_address.h"
#include "r_utils/r_exception.h"

#ifdef IS_WINDOWS

    #include <WinSock2.h>
    #include <ws2tcpip.h>
    #include <Iphlpapi.h>

    typedef int SOK;

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

    typedef int SOK;

    #define SOCKET_SHUT_FLAGS SHUT_RDWR
    #define SOCKET_SHUT_SEND_FLAGS SHUT_WR
    #define SOCKET_SHUT_RECV_FLAGS SHUT_RD

#endif

#include <memory>
#include <map>
#include <mutex>
#include <vector>
#include <algorithm>
#include <string.h>
#include <sys/types.h>

class test_r_utils;

namespace r_utils
{

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

    R_API inline SOK get_sok_id() const { return _sok; }

    R_API virtual bool valid() const
	{
		return (_sok > 0) ? true : false;
	}

    R_API virtual int send(const void* buf, size_t len);
    R_API virtual int recv(void* buf, size_t len);

    R_API virtual void close() const;

    R_API virtual bool wait_till_recv_wont_block( uint64_t& millis ) const;
    R_API virtual bool wait_till_send_wont_block( uint64_t& millis ) const;

    R_API std::string get_peer_ip() const;
    R_API std::string get_local_ip() const;

protected:
    mutable SOK _sok;
    r_socket_address _addr;
    std::string _host;

    static bool _sokSysStarted;
    static std::recursive_mutex _sokLock;
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

    R_API inline SOK get_sok_id() const { return _sok.get_sok_id(); }

    R_API inline virtual bool valid() const { return _sok.valid(); }

    R_API virtual int send( const void* buf, size_t len );

    R_API virtual int recv( void* buf, size_t len );

    R_API inline void close() const { _sok.close(); }

    R_API inline virtual bool wait_till_recv_wont_block( uint64_t& millis ) const { return _sok.wait_till_recv_wont_block(millis); }
    R_API inline virtual bool wait_till_send_wont_block( uint64_t& millis ) const { return _sok.wait_till_send_wont_block(millis); }

    R_API inline std::string get_peer_ip() const { return _sok.get_peer_ip(); }
    R_API inline std::string get_local_ip() const { return _sok.get_local_ip(); }

private:
    mutable r_raw_socket _sok;
    uint64_t _ioTimeOut;
};

template<class SOK>
class r_buffered_socket : public r_socket_base
{
public:
    friend class ::test_r_utils;

public:
    enum r_buffered_socket_defaults
    {
        MAX_BACKLOG = 5
    };

    inline r_buffered_socket( size_t bufferSize = 4096 ) :
        _sok(),
        _buffer()
    {
        _buffer.reserve(bufferSize);
    }

    inline r_buffered_socket( r_buffered_socket&& obj ) noexcept :
        _sok( std::move(obj._sok) ),
        _buffer( std::move(obj._buffer) )
    {
    }

    r_buffered_socket( const r_buffered_socket& ) = delete;

    inline virtual ~r_buffered_socket() noexcept {}

    inline r_buffered_socket& operator = ( r_buffered_socket&& obj ) noexcept
    {
        if(valid())
            close();

        _sok = std::move(obj._sok);
        _buffer = std::move(obj._buffer);
        return *this;
    }

    r_buffered_socket& operator = ( const r_buffered_socket& ) = delete;

    // Returns a reference to the underlying socket. This is especially useful if the underlying socket
    // type is SSL, in which case we don't have t a full API implemented.
    SOK& inner() { return _sok; }

    inline void create( int af ) { _sok.create(af); }

    inline virtual void connect( const std::string& host, int port ) { _sok.connect(host, port); }
    inline void listen( int backlog = MAX_BACKLOG ) { _sok.listen(backlog); }
    inline void bind( int port, const std::string& ip = "" ) { _sok.bind(port, ip ); }
    inline r_buffered_socket accept() { r_buffered_socket bs(_buffer.capacity()); auto s = _sok.accept(); bs._sok = std::move(s); return bs; }

    inline SOK get_sok_id() const { return _sok.get_sok_id(); }

    bool buffer_recv()
    {
        if (_buffer.size() > 0)
            return true;

        auto cap = _buffer.capacity();
        _buffer.resize(cap);

        int bytesRead = _sok.recv(&_buffer[0], cap);
        
        if (bytesRead < 0) {
            _buffer.resize(0);
            return false;  // Indicate error
        }
        
        _buffer.resize(bytesRead);
        return bytesRead > 0;  // Return true if we read anything
    }

    inline virtual bool valid() const { return _sok.valid(); }

    virtual int send( const void* buf, size_t len )
    {
        return _sok.send( buf, len );
    }

    virtual int recv( void* buf, size_t len )
    {
        if (!buffer_recv())
            return 0;  // No data available or error occurred

        auto avail = (int)_buffer.size();
        
        if (avail > 0)
        {
            auto bytesToRead = (int)std::min((size_t)avail, len);
            
            // Copy data to user buffer
            std::copy(_buffer.begin(), _buffer.begin() + bytesToRead, static_cast<uint8_t*>(buf));
            
            // Remove read data from buffer
            // Option 1: Using erase (clearer)
            _buffer.erase(_buffer.begin(), _buffer.begin() + bytesToRead);
            
            // Option 2: Using rotate and resize (original approach)
            // auto sentry = _buffer.begin() + bytesToRead;
            // std::rotate(_buffer.begin(), sentry, _buffer.end());
            // _buffer.resize(avail - bytesToRead);
            
            return bytesToRead;
        }
        
        return 0;
    }

    inline virtual void close() const { _sok.close(); }

    inline virtual bool wait_till_recv_wont_block( uint64_t& millis ) const { return _sok.wait_till_recv_wont_block(millis); }
    inline virtual bool wait_till_send_wont_block( uint64_t& millis ) const { return _sok.wait_till_send_wont_block(millis); }

    inline std::string get_peer_ip() const { return _sok.get_peer_ip(); }
    inline std::string get_local_ip() const { return _sok.get_local_ip(); }

    inline SOK& get_socket() { return _sok; }

private:
    mutable SOK _sok;
    std::vector<uint8_t> _buffer;

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
