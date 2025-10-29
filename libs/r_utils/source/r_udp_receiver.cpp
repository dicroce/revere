
#include "r_utils/r_udp_receiver.h"
#ifdef IS_LINUX
#include <unistd.h>
#endif
#ifdef IS_WINDOWS
    #include <WinSock2.h>
    #include <ws2tcpip.h>
    #include <Iphlpapi.h>
#endif
#include <chrono>
using namespace r_utils;
using namespace std;

r_udp_receiver::r_udp_receiver( int destinationPort,
                                int recvBufferSize,
                                const string& destinationAddress ) :
    _sok( 0 ),
    _addr( destinationPort, destinationAddress ),
    _associatedReceivers()
{
    // Map wildcard addresses (0.0.0.0 or ::) to the addr_any type.
    if( _addr.is_wildcard_address() )
        _addr.set_address( _addr.is_ipv4() ? ip4_addr_any : ip6_addr_any );

    // First, create our datagram socket...
    _sok = (SOK)socket( _addr.address_family(), SOCK_DGRAM, IPPROTO_UDP );
    if( _sok <= 0 )
        R_STHROW(r_internal_exception, ( "r_udp_receiver: Unable to create a datagram socket." ));

    int on = 1;

    int err = (int)::setsockopt( (SOK)_sok, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(int) );

    if( err < 0 )
        R_STHROW(r_internal_exception, ( "r_udp_receiver: Unable to configure socket." ));

    if( recvBufferSize > 0 )
    {
        err = setsockopt( _sok, SOL_SOCKET, SO_RCVBUF, (char*)&recvBufferSize, sizeof( recvBufferSize ) );

        if( err < 0 )
            R_STHROW(r_internal_exception, ( "r_udp_receiver: Unable to configure socket rcvbuf." ));
    }

    if( _addr.is_ipv6() )
    {
       int ipv6only;
       socklen_t optlen = sizeof( ipv6only );
       if( getsockopt( _sok, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&ipv6only, &optlen ) == 0 )
       {
          ipv6only = 0;
          if( setsockopt( _sok, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&ipv6only, sizeof(ipv6only) ) != 0 )
             R_STHROW(r_internal_exception, ( "Failed to set IPV6_V6ONLY socket option." ));
       }
       else
          R_LOG_WARNING( "[r_udp_receiver]: IPV6_V6ONLY socket option not supported on this platform" );
    }

    if( r_socket_address::is_multicast( destinationAddress ) )
    {
        if( ::bind( _sok, _addr.get_sock_addr(), _addr.sock_addr_size() ) < 0 )
            R_STHROW(r_internal_exception, ( "r_udp_receiver: Unable to bind to local interface." ));
    }
    else
    {
        if( ::bind( _sok, _addr.get_sock_addr(), _addr.sock_addr_size()) < 0 )
            R_STHROW(r_internal_exception, ( "r_udp_receiver: Unable to bind to local interface." ));
    }

    // If an r_udp_receiver is created with a default destinationPort (0), then use getsockname()
    // to discover the actual local port that was used by bind() and then SET that port on _addr.
    // This allows a subsequent call to GetBoundPort() to return a discovered free port rather than
    // 0. If the destinationPort was valid and we bound to it, all this does is reset _addr to the
    // value it already had.
    sockaddr_storage sa;
    memset( &sa, 0, sizeof( sa ) );
    int size = sizeof( sa );
    getsockname( _sok, (sockaddr*)&sa, (socklen_t*)&size );
    if( sa.ss_family == AF_INET )
        _addr.set_port_num( ntohs(((sockaddr_in*)&sa)->sin_port) );
    else _addr.set_port_num( ntohs(((sockaddr_in6*)&sa)->sin6_port) );

    if( _addr.address().length() > 0 )
    {
        if( r_socket_address::is_multicast( _addr.address() ) )
        {
            int level, optname, optlen;
            char *optval = 0;

            if( _addr.address_family() == AF_INET )
            {
                struct ip_mreq v4mreq;
                level = IPPROTO_IP;
                optname = IP_ADD_MEMBERSHIP;
                optval = (char*)&v4mreq;
                optlen = sizeof( v4mreq );
                memset( &v4mreq, 0, sizeof( v4mreq ) );
                v4mreq.imr_multiaddr.s_addr = ((struct sockaddr_in*)_addr.get_sock_addr())->sin_addr.s_addr;
                v4mreq.imr_interface.s_addr = htonl( INADDR_ANY );
            }
            else
            {
                struct ipv6_mreq v6mreq;
                level = IPPROTO_IPV6;
                optname = IPV6_JOIN_GROUP;
                optval = (char*)&v6mreq;
                optlen = sizeof( v6mreq );
                memset( &v6mreq, 0, sizeof( v6mreq ) );
                v6mreq.ipv6mr_multiaddr = ((struct sockaddr_in6*)_addr.get_sock_addr())->sin6_addr;
                v6mreq.ipv6mr_interface = 0;
            }

            setsockopt( _sok, level, optname, optval, optlen );
        }
    }
}

/// Note:
/// connect() can be used to filter incoming UDP packets by ip address. Note: the port passed to
/// connect is the SRC port. You probably want to pass 0 for the port here.
void r_udp_receiver::connect( const string& ipAddress, int port )
{
    r_socket_address addr( port, ipAddress );

    if( ::connect( _sok, addr.get_sock_addr(), addr.sock_addr_size() ) )
    {
        R_STHROW(r_internal_exception, ( "r_udp_receiver::connect: Failed to connect to %s:%d",
                   ipAddress.c_str(),
                   port ));
    }
}

bool r_udp_receiver::receive( int& port, vector<uint8_t>& buffer )
{
    return _receive( port, buffer, true, 0 );
}

bool r_udp_receiver::receive( int& port, vector<uint8_t>& buffer, int waitMillis )
{
    // We could accept lower values, but if you really want them that low, you
    // probably should be doing a non-blocking call, and if a low enough value
    // is given to select, then it never times out (e.g. with some testing on Windows,
    // it seemed to work with 10ms but not 1ms, and it might vary from machine to
    // machine or OS to OS). And passing 250ms to select works well in almost all
    // cases, so we don't want to mess with that without good reason.

    return _receive( port, buffer, true, waitMillis );
}

bool r_udp_receiver::non_blocking_receive( int& port, vector<uint8_t>& buffer )
{
    return _receive( port, buffer, false, 0 );
}

bool r_udp_receiver::_receive( int& port, vector<uint8_t>& buffer, bool block, int waitMillis )
{
    fd_set readFileDescriptors;
    int selectRet = 0;

    auto beforeSelect = std::chrono::steady_clock::now();

    while( (_sok > 0) && (selectRet == 0) )
    {
        struct timeval tv = { 0, block ? 250000 : 0 };

        FD_ZERO( &readFileDescriptors );

        SOK currentLargestSOK = _sok;

        // First, add ourseleves...
        FD_SET( _sok, &readFileDescriptors );

        // Next, loop over all the associated sockets and add them to the set.
        std::list<shared_ptr<r_udp_receiver> >::iterator i;
        for( i = _associatedReceivers.begin(); i != _associatedReceivers.end(); i++ )
        {
            FD_SET( (*i)->_sok, &readFileDescriptors );

            if( (*i)->_sok > currentLargestSOK )
                currentLargestSOK = (*i)->_sok;
        }

        selectRet = select( (int)currentLargestSOK + 1,
                            &readFileDescriptors,
                            nullptr,
                            nullptr,
                            &tv );

        // Check for timeout (if one is provided)
        if( (waitMillis > 0) && (selectRet == 0) )
        {
            auto afterSelect = std::chrono::steady_clock::now();

            auto deltaMillis = std::chrono::duration_cast<std::chrono::milliseconds>(afterSelect - beforeSelect).count();

            if( ((waitMillis - deltaMillis) <= 0) )
                return false;
        }
        else if( !block )
            break;
    }

    if( selectRet > 0 )
    {
        if( FD_ISSET( _sok, &readFileDescriptors ) )
        {
            unsigned char bytes[2048];

            int bytesReceived = recv( _sok,
                                      (char*)bytes,
                                      2048,
                                      0 );
            if( _sok <= 0 )
                return false;

            if( bytesReceived <= 0 )
                return false;

            buffer.resize(bytesReceived);

            memcpy( &buffer[0], bytes, bytesReceived );

            port = _addr.port();

            return true;
        }
        else
        {
            for( auto i = _associatedReceivers.begin(); i != _associatedReceivers.end(); i++)
            {
                shared_ptr<r_udp_receiver> receiver = *i;

                if(FD_ISSET(receiver->_sok, &readFileDescriptors))
                    return receiver->receive(port, buffer);
            }
        }
    }

    return false;
}

bool r_udp_receiver::raw_receive(int& port, vector<uint8_t>& buffer)
{
    buffer.resize( 2048 );

    socklen_t sockLen = _addr.sock_addr_size();
    int bytesReceived = recvfrom( _sok,
                                  (char*)&buffer[0],
                                  2048,
                                  0,
                                  _addr.get_sock_addr(),
                                  &sockLen );

    buffer.resize((bytesReceived <= 0) ? 0 : bytesReceived);
    //buffer->set_data_size( (bytesReceived <= 0) ? 0 : bytesReceived );

    if( _sok <= 0 )
        return false;

    if( bytesReceived <= 0 )
        return false;

    return true;
}

void r_udp_receiver::shutdown()
{
    ::shutdown( _sok, SOCKET_SHUT_FLAGS );
}

void r_udp_receiver::close()
{
    // I'm adding this because I have seen cases where close() alone is not
    // enough to unblock a recvfrom() on another thread, but shutdown() does
    // the trick. on linux!
    shutdown();

    SOK tmpSok = _sok;

    _sok = 0;

    FULL_MEM_BARRIER();

#ifdef IS_LINUX
    if( tmpSok != 0 )
        ::close( tmpSok );
#endif
#ifdef IS_WINDOWS
    if(tmpSok != 0)
        ::closesocket(tmpSok);
#endif
}

void r_udp_receiver::associate( shared_ptr<r_udp_receiver> receiver )
{
    _associatedReceivers.push_back( receiver );
}

void r_udp_receiver::clear_associations()
{
    _associatedReceivers.clear();
}

int r_udp_receiver::get_bound_port()
{
    return _addr.port();
}

size_t r_udp_receiver::get_recv_buffer_size()
{
    size_t size = 0;
    socklen_t sizelen = sizeof( size );

    if( getsockopt( _sok, SOL_SOCKET, SO_RCVBUF, (char*)&size, &sizelen ) != 0 )
        R_STHROW(r_internal_exception, ( "r_udp_receiver::GetRecvBufferSize: Failed to get the buffer size." ));
    return size;
}

void r_udp_receiver::set_recv_buffer_size(size_t size)
{
    if( setsockopt( _sok, SOL_SOCKET, SO_RCVBUF, (char*)&size, sizeof(size) ) != 0 )
        R_STHROW(r_internal_exception, ( "r_udp_receiver::SetRecvBufferSize: Failed to set the buffer size." ));
}
