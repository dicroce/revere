#include "r_utils/r_udp_sender.h"
#include "r_utils/r_exception.h"

#ifdef IS_WINDOWS
    #include <WinSock2.h>
    #include <ws2tcpip.h>
    #include <Iphlpapi.h>
#endif

using namespace r_utils;
using namespace std;

r_udp_sender::r_udp_sender( const string& targetIP,
                            int targetPort,
                            const string& localInterfaceIP,
                            int localPort ) :
  _sok( 0 ),
  _addr( targetPort, targetIP ),
  _localInterfaceIP( localInterfaceIP ),
  _localPort( localPort )
{
    _configure();
}

r_udp_sender::~r_udp_sender() noexcept
{
    _close();
}

void r_udp_sender::aim( const string& targetIP, int targetPort )
{
    _addr.set_port_num( targetPort );
    _addr.set_address( targetIP );

    _configure();
}

int32_t r_udp_sender::send( void* buffer, size_t length )
{
  int32_t bytesSent = sendto( (int)_sok,
			                  (char*)buffer,
			                  (int)length,
			                  0,
			                  _addr.get_sock_addr(),
			                  _addr.sock_addr_size() );

    if( bytesSent == -1 )
        R_STHROW(r_internal_exception, ( "r_udp_sender: sendto() returned an error." ));

    return bytesSent;
}

void r_udp_sender::_configure()
{
    _close();

    // First, create our datagram socket...
    _sok = (SOK)socket( _addr.address_family(), SOCK_DGRAM, IPPROTO_UDP );
    if( _sok < 0 )
        R_STHROW(r_internal_exception, ( "r_udp_sender: Unable to create a datagram socket." ));

    // Now, we'd like this socket to optionally use the local interface
    // identified by "localInterfaceIP" if one was passed, or INADDR_ANY
    // if a local interface was not specified.
    r_socket_address addr( _localPort );
    if (_localInterfaceIP.length() > 0)
        addr.set_address( _localInterfaceIP );
    else
        addr.set_address( _addr.address_family() == AF_INET ? ip4_addr_any : ip6_addr_any );

    // This call binds our UDP socket to a particular local interface,
    // OR any local interface if localInterfaceIP == "INADDR_ANY" (the
    // default).
    if( bind( _sok, addr.get_sock_addr(), addr.sock_addr_size() ) < 0 )
        R_STHROW(r_internal_exception, ( "r_udp_sender: Unable to bind to local interface." ));
}

void r_udp_sender::_close() throw()
{
#ifdef IS_LINUX
    if( _sok != 0 )
        ::close( _sok );
#endif
#ifdef IS_WINDOWS
    if( _sok != 0 )
        ::closesocket( _sok );
#endif
    _sok = 0;
}

size_t r_udp_sender::get_send_buffer_size()
{
    size_t size = 0;
    socklen_t sizelen = sizeof( size );
    const int err = getsockopt( _sok, SOL_SOCKET, SO_SNDBUF, (char*)&size, &sizelen );
    if (err)
        R_STHROW(r_internal_exception, ( "r_udp_sender::GetRecvBufferSize: Failed to get the buffer size." ));

    return size;
}

void r_udp_sender::set_send_buffer_size( size_t size )
{
    const int err = setsockopt( _sok, SOL_SOCKET, SO_SNDBUF, (char*)&size, sizeof(size) );
    if (err)
        R_STHROW(r_internal_exception, ( "r_udp_sender::SetRecvBufferSize: Failed to set the buffer size." ));
}
