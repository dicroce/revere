
#ifndef r_utils_r_socket_h
#define r_utils_r_socket_h

#include "r_utils/interfaces/r_stream_io.h"
#include "r_utils/interfaces/r_socket_io.h"
#include "r_utils/interfaces/r_pollable.h"
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
#include <string.h>
#include <sys/types.h>

class test_r_utils;

namespace r_utils
{

class r_raw_socket : public r_socket_io, public r_pollable
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

    R_API void connect( const std::string& host, int port );
    R_API void listen( int backlog = MAX_BACKLOG );
    R_API void bind( int port, const std::string& ip = "" );
    R_API r_raw_socket accept();

    R_API inline SOK get_sok_id() const { return _sok; }

    R_API inline bool valid() const
	{
		return (_sok > 0) ? true : false;
	}

    R_API virtual int raw_send( const void* buf, size_t len );
    R_API virtual int raw_recv( void* buf, size_t len );

    R_API void close() const;

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

class r_socket : public r_stream_io, public r_pollable
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

    R_API void connect( const std::string& host, int port );
    R_API inline void listen( int backlog = MAX_BACKLOG ) { _sok.listen(backlog); }
    R_API inline void bind( int port, const std::string& ip = "" ) { 
        _sok.bind(port, ip ); 
    }
    R_API inline r_socket accept() { auto r = _sok.accept(); r_socket s; s._sok = std::move(r); return s; }

    R_API inline SOK get_sok_id() const { return _sok.get_sok_id(); }

    R_API virtual int raw_send( const void* buf, size_t len );

    R_API virtual int raw_recv( void* buf, size_t len );

    R_API inline virtual bool valid() const { return _sok.valid(); }

    R_API virtual void send( const void* buf, size_t len );

    R_API virtual void recv( void* buf, size_t len );

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
class r_buffered_socket : public r_stream_io, public r_pollable
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
        _buffer(),
        _bufferOff(0)
    {
        _buffer.reserve(bufferSize);
    }

    inline r_buffered_socket( r_buffered_socket&& obj ) noexcept :
        _sok( std::move(obj._sok) ),
        _buffer( std::move(obj._buffer) ),
        _bufferOff( std::move( obj._bufferOff ) )
    {
        obj._bufferOff = 0;
    }

    r_buffered_socket( const r_buffered_socket& ) = delete;

    inline virtual ~r_buffered_socket() noexcept {}

    inline r_buffered_socket& operator = ( r_buffered_socket&& obj ) noexcept
    {
        if(valid())
            close();

        _sok = std::move(obj._sok);
        _buffer = std::move(obj._buffer);
        _bufferOff = std::move(obj._bufferOff);
        obj._bufferOff = 0;
        return *this;
    }

    r_buffered_socket& operator = ( const r_buffered_socket& ) = delete;

    // Returns a reference to the underlying socket. This is especially useful if the underlying socket
    // type is SSL, in which case we don't have t a full API implemented.
    SOK& inner() { return _sok; }

    inline void create( int af ) { _sok.create(af); }

    inline void connect( const std::string& host, int port ) { _sok.connect(host, port); }
    inline void listen( int backlog = MAX_BACKLOG ) { _sok.listen(backlog); }
    inline void bind( int port, const std::string& ip = "" ) { _sok.bind(port, ip ); }
    inline r_buffered_socket accept() { r_buffered_socket bs(_buffer.capacity()); auto s = _sok.accept(); bs._sok = std::move(s); return bs; }

    inline SOK get_sok_id() const { return _sok.get_sok_id(); }

    bool buffer_recv()
    {
        if( _avail_in_buffer() > 0 )
            return true;

        size_t bufferCapacity = _buffer.capacity();

        _buffer.resize( bufferCapacity );

        int bytesRead = _sok.raw_recv( &_buffer[0], bufferCapacity );

        if( bytesRead >= 0 )
        {
            _buffer.resize( bytesRead );
            _bufferOff = 0;
        }

        return (bytesRead > 0) ? true : false;
    }

    inline virtual bool valid() const { return _sok.valid(); }

    virtual void send( const void* buf, size_t len )
    {
        _sok.send( buf, len );
    }

    virtual void recv( void* buf, size_t len )
    {
        size_t bytesToRecv = len;
        uint8_t* dst = (uint8_t*)buf;

        while( bytesToRecv > 0 )
        {
            size_t avail = _avail_in_buffer();

            if( avail == 0 )
            {
                buffer_recv();
                continue;
            }

            size_t ioSize = (bytesToRecv <= avail) ? bytesToRecv : avail;

            memcpy( dst, &_buffer[_bufferOff], ioSize );
            dst += ioSize;
            _bufferOff += ioSize;
            bytesToRecv -= ioSize;
        }
    }

    inline void close() const { _sok.close(); }

    inline virtual bool wait_till_recv_wont_block( uint64_t& millis ) const { return _sok.wait_till_recv_wont_block(millis); }
    inline virtual bool wait_till_send_wont_block( uint64_t& millis ) const { return _sok.wait_till_send_wont_block(millis); }

    inline std::string get_peer_ip() const { return _sok.get_peer_ip(); }
    inline std::string get_local_ip() const { return _sok.get_local_ip(); }

    inline SOK& get_socket() { return _sok; }

private:
    inline size_t _avail_in_buffer() { return _buffer.size() - _bufferOff; }

    mutable SOK _sok;
    std::vector<uint8_t> _buffer;
    size_t _bufferOff;
};

class r_socket_connect_exception : public r_exception
{
public:
    r_socket_connect_exception(const char* msg, ...);
    virtual ~r_socket_connect_exception() noexcept {}
};

class r_socket_exception : public r_exception
{
public:
    r_socket_exception(const char* msg, ...);
    virtual ~r_socket_exception() noexcept {}
};

namespace r_networking
{

std::vector<std::string> r_resolve( int type, const std::string& name );

std::vector<uint8_t> r_get_hardware_address(const std::string& ifname);

std::string r_get_device_uuid(const std::string& ifname);

uint16_t r_ntohs(uint16_t x);

uint16_t r_htons(uint16_t x);

uint32_t r_ntohl(uint32_t x);

uint32_t r_htonl(uint32_t x);

uint64_t r_ntohll(uint64_t x);

uint64_t r_htonll(uint64_t x);

}

}

#endif
