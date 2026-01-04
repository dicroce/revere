
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_md5.h"

#ifdef IS_LINUX
#include <ifaddrs.h>
#include <linux/if.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#endif

#ifdef IS_MACOS
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#endif

using namespace r_utils;
using namespace std;

static const int POLL_NFDS = 1;

bool r_raw_socket::_sokSysStarted = false;
recursive_mutex r_raw_socket::_sokLock;

void r_raw_socket::socket_startup()
{
#ifdef IS_WINDOWS
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    wVersionRequested = MAKEWORD( 2, 2 );

    err = WSAStartup( wVersionRequested, &wsaData );
    if ( err != 0 )
        R_STHROW( r_socket_exception, ( "Unable to load WinSock DLL"));

    if ( LOBYTE( wsaData.wVersion ) != 2 ||
         HIBYTE( wsaData.wVersion ) != 2 )
    {
        R_STHROW( r_socket_exception, ( "Unable to load WinSock DLL"));
    }
#endif
}

void r_raw_socket::socket_cleanup()
{
#ifdef WIN32
    WSACleanup();
#endif
}

r_raw_socket::r_raw_socket() :
    _sok( -1 ),
    _addr( 0 ),
    _host()
{
}

r_raw_socket::r_raw_socket( r_raw_socket&& obj ) noexcept :
    _sok( -1 ),
    _instanceLock(),
    _addr(),
    _host()
{
    // Lock the source object to ensure thread-safe move
    std::lock_guard<std::recursive_mutex> lock(obj._instanceLock);
    _sok = obj._sok;
    _addr = std::move( obj._addr );
    _host = std::move( obj._host );
    obj._sok = -1;
    obj._host = string();
}

r_raw_socket::~r_raw_socket() noexcept
{
    if( valid() )
        close();
}

r_raw_socket& r_raw_socket::operator = ( r_raw_socket&& obj ) noexcept
{
    if(this == &obj)
        return *this;

    // Lock both objects - always lock in consistent order to prevent deadlock
    // Use pointer comparison to determine lock order
    std::recursive_mutex* first = &_instanceLock;
    std::recursive_mutex* second = &obj._instanceLock;
    if(first > second)
        std::swap(first, second);

    std::lock_guard<std::recursive_mutex> lock1(*first);
    std::lock_guard<std::recursive_mutex> lock2(*second);

    if(_sok > 0)
    {
        // Close the current socket without recursively locking
        SOK sokTemp = _sok;
        _sok = -1;
        FULL_MEM_BARRIER();
#ifdef IS_WINDOWS
        ::closesocket(sokTemp);
#else
        ::close(sokTemp);
#endif
    }

    _sok = obj._sok;
    obj._sok = -1;
    _addr = std::move(obj._addr);
    _host = std::move( obj._host );
    obj._host = string();
    return *this;
}

void r_raw_socket::create( int af )
{
    _sok = (SOK) ::socket( af, SOCK_STREAM, 0 );

    if( _sok <= 0 )
        R_STHROW( r_socket_exception, ("Unable to create socket.") );

    int on = 1;
    if( ::setsockopt( (SOK)_sok, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(int) ) < 0 )
        R_STHROW( r_socket_exception, ("Unable to configure socket.") );
}

void r_raw_socket::connect( const string& host, int port )
{
    if( !valid() )
        create( r_socket_address::get_address_family(host) );

    _host = host;

    _addr.set_address( host, port );

    int err = ::connect( _sok, _addr.get_sock_addr(), _addr.sock_addr_size());

	if (err < 0)
	{
		if(valid())
			R_STHROW(r_socket_connect_exception, ("Unable to connect to %s:%d.", _host.c_str(), port));
	}
}

void r_raw_socket::listen( int backlog )
{
    if( ::listen( _sok, backlog ) < 0 )
        R_STHROW( r_socket_exception, ( "Unable to listen on bound port") );
}

void r_raw_socket::bind( int port, const string& ip )
{
    _addr.set_address( ip, port );

    if( !valid() )
        create( _addr.address_family() );

    if( ::bind( _sok, _addr.get_sock_addr(), _addr.sock_addr_size() ) )
        R_STHROW( r_socket_exception, ( "Unable to bind given port and IP.") );
}

r_raw_socket r_raw_socket::accept()
{
    if( _sok <= 0 )
        R_STHROW( r_socket_exception, ( "Unable to accept() on uninitialized socket." ));

    r_raw_socket clientSocket;

    SOK clientSok = 0;
    socklen_t addrLength = _addr.sock_addr_size();

    clientSok = (SOK)::accept(_sok,
                              _addr.get_sock_addr(),
                              &addrLength );

    // Since the socket can be closed by another thread while we were waiting in accept(),
    // we only throw here if _sok is still a valid fd.
    if( valid() && clientSok <= 0 )
        R_STHROW( r_socket_exception, ( "Unable to accept inbound connection."));

    clientSocket._sok = clientSok;

    return clientSocket;
}

#if !defined MSG_NOSIGNAL
# define MSG_NOSIGNAL 0
#endif

int r_raw_socket::send( const void* buf, size_t len )
{
    return (int)::send(_sok, (char*)buf, (int)len, MSG_NOSIGNAL);
}

int r_raw_socket::recv( void* buf, size_t len )
{
    return (int)::recv(_sok, (char*)buf, (int)len, 0);
}

void r_raw_socket::close() const
{
    std::lock_guard<std::recursive_mutex> lock(_instanceLock);

    if( _sok < 0 )
        return;

    SOK sokTemp = _sok;
    int err;

    _sok = -1;

    FULL_MEM_BARRIER();

#ifdef IS_WINDOWS
    err = ::closesocket(sokTemp);
#else
    err = ::close(sokTemp);
#endif
    if( err < 0 )
        R_LOG_WARNING( "Failed to close socket." );
}

SOK r_raw_socket::get_sok_id() const
{
    std::lock_guard<std::recursive_mutex> lock(_instanceLock);
    return _sok;
}

bool r_raw_socket::valid() const
{
    std::lock_guard<std::recursive_mutex> lock(_instanceLock);
    return (_sok > 0) ? true : false;
}

bool r_raw_socket::wait_till_recv_wont_block( uint64_t& millis ) const
{
    // Get socket ID under lock, then release before blocking on select()
    SOK sok;
    {
        std::lock_guard<std::recursive_mutex> lock(_instanceLock);
        sok = _sok;
        if(sok < 0)
            return false;
    }

    struct timeval recv_timeout;
    recv_timeout.tv_sec = (uint32_t)(millis / 1000);
    recv_timeout.tv_usec = (uint32_t)((millis % 1000) * 1000);

    fd_set recv_fds;
    FD_ZERO(&recv_fds);
    FD_SET((int)sok, &recv_fds);

    auto before = std::chrono::steady_clock::now();

    auto fds_with_data = select((int)(sok + 1), &recv_fds, NULL, NULL, &recv_timeout);

    auto after = std::chrono::steady_clock::now();

    // Check if socket was closed while we were waiting
    {
        std::lock_guard<std::recursive_mutex> lock(_instanceLock);
        if(_sok < 0)
            return false;
    }

    uint64_t elapsed_millis = std::chrono::duration_cast<std::chrono::milliseconds>(after - before).count();

    millis -= (elapsed_millis >= millis) ? millis : elapsed_millis;

    return (fds_with_data > 0) ? true : false;
}

bool r_raw_socket::wait_till_send_wont_block( uint64_t& millis ) const
{
    // Get socket ID under lock, then release before blocking on select()
    SOK sok;
    {
        std::lock_guard<std::recursive_mutex> lock(_instanceLock);
        sok = _sok;
        if(sok < 0)
            return false;
    }

    struct timeval send_timeout;
    send_timeout.tv_sec = (uint32_t)(millis / 1000);
    send_timeout.tv_usec = (uint32_t)((millis % 1000) * 1000);

    fd_set send_fds;
    FD_ZERO(&send_fds);
    FD_SET((int)sok, &send_fds);

    auto before = std::chrono::steady_clock::now();

    auto fds_with_data = select((int)(sok + 1), NULL, &send_fds, NULL, &send_timeout);

    auto after = std::chrono::steady_clock::now();

    // Check if socket was closed while we were waiting
    {
        std::lock_guard<std::recursive_mutex> lock(_instanceLock);
        if(_sok < 0)
            return false;
    }

    uint64_t elapsed_millis = std::chrono::duration_cast<std::chrono::milliseconds>(after - before).count();

    millis -= (elapsed_millis >= millis) ? millis : elapsed_millis;

    return (fds_with_data > 0) ? true : false;
}

string r_raw_socket::get_peer_ip() const
{
    struct sockaddr_storage peer;
    int peerLength = sizeof(peer);

    if ( getpeername(_sok,(sockaddr*)&peer,(socklen_t*)&peerLength) < 0 )
    {
        R_LOG_WARNING("Unable to get peer ip.");
        return string();
    }

    return r_socket_address::address_to_string((sockaddr*)&peer, (socklen_t)peerLength);
}

string r_raw_socket::get_local_ip() const
{
    struct sockaddr_storage local;
    int addrLength = sizeof(local);

    if ( getsockname(_sok, (sockaddr*)&local, (socklen_t*)&addrLength) < 0 )
    {
        R_LOG_WARNING("Unable to get local ip.");
        return "";
    }

    return r_socket_address::address_to_string((sockaddr*)&local, (socklen_t)addrLength);
}

void r_socket::connect( const string& host, int port )
{
    if( !valid() )
        create( r_socket_address::get_address_family(host) );

    timeval connect_timeout;
    connect_timeout.tv_sec = (long)(_ioTimeOut / 1000);
    connect_timeout.tv_usec = (long)((_ioTimeOut % 1000) * 1000);

    if( ::setsockopt( (SOK)_sok.get_sok_id(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&connect_timeout, sizeof(connect_timeout) ) < 0 )
        R_STHROW( r_socket_exception, ("Unable to configure socket.") );

    if( ::setsockopt( (SOK)_sok.get_sok_id(), SOL_SOCKET, SO_SNDTIMEO, (const char*)&connect_timeout, sizeof(connect_timeout) ) < 0 )
        R_STHROW( r_socket_exception, ("Unable to configure socket.") );

    _sok.connect(host, port);
}

int r_socket::send( const void* buf, size_t len )
{
    return _sok.send(buf, len);
}

int r_socket::recv( void* buf, size_t len )
{
    return _sok.recv(buf, len);
}

r_socket_connect_exception::r_socket_connect_exception(const char* msg, ...) :
    r_exception()
{
    va_list args;
    va_start(args, msg);
    _msg = r_string_utils::format(msg, args);
    va_end(args);
}

r_socket_exception::r_socket_exception(const char* msg, ...) :
    r_exception()
{
    va_list args;
    va_start(args, msg);
    _msg = r_string_utils::format(msg, args);
    va_end(args);
}

int r_utils::r_networking::r_send(r_socket_base& sok, const void* buf, size_t len, uint64_t millis)
{
    int bytesToSend = (int)len;
    const uint8_t* reader = (uint8_t*)buf;

    while(sok.valid() && bytesToSend > 0 && millis > 0)
    {
        if(sok.wait_till_send_wont_block(millis))
        {
            int bytesJustSent = sok.send(reader, bytesToSend);
            if(bytesJustSent <= 0)
            {
                return (int)(len - bytesToSend);
            }
            else
            {
                reader += bytesJustSent;
                bytesToSend -= bytesJustSent;
            }
        }
        else R_STHROW(r_socket_exception, ("send timeout"));
    }
    
    return (int)(len - bytesToSend);
}

int r_utils::r_networking::r_recv(r_socket_base& sok, void* buf, size_t len, uint64_t millis)
{
    int bytesToRecv = (int)len;
    uint8_t* writer = (uint8_t*)buf;

    while(sok.valid() && bytesToRecv > 0 && millis > 0)
    {
        if(sok.wait_till_recv_wont_block(millis))
        {
            int bytesJustRecv = sok.recv(writer, bytesToRecv);
            if(bytesJustRecv <= 0)
            {
                return (int)(len - bytesToRecv);
            }
            else
            {
                writer += bytesJustRecv;
                bytesToRecv -= bytesJustRecv;
            }
        }
        else R_STHROW(r_socket_exception, ("recv timeout"));
    }

    return (int)(len - bytesToRecv);
}

vector<string> r_utils::r_networking::r_resolve( int type, const string& name )
{
    vector<string> addresses;

    struct addrinfo hints, *addrInfo = nullptr;
    memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_CANONNAME;// | AI_NUMERICHOST;

    int err = getaddrinfo( name.c_str(), 0, &hints, &addrInfo );
    if (err)
        R_STHROW( r_socket_exception, ("Failed to resolve address by hostname.") );

    for( struct addrinfo* cur = addrInfo; cur != 0; cur = cur->ai_next )
    {
        // We're only interested in IPv4 and IPv6
        if( (cur->ai_family != AF_INET) && (cur->ai_family != AF_INET6) )
            continue;

        if( cur->ai_addr->sa_family == type )
            addresses.push_back( r_socket_address::address_to_string(cur->ai_addr, (socklen_t)cur->ai_addrlen) );
    }

    freeaddrinfo( addrInfo );

    return addresses;
}

vector<uint8_t> r_utils::r_networking::r_get_hardware_address(
    const string&
#if defined(IS_LINUX) || defined(IS_MACOS)
    ifname
#endif
)
{
    vector<uint8_t> buffer(6);

#ifdef IS_WINDOWS
    // Get the first active network adapter's MAC address
    ULONG bufferSize = 15000;  // Recommended initial size
    vector<uint8_t> adapterInfoBuffer(bufferSize);

    PIP_ADAPTER_INFO pAdapterInfo = reinterpret_cast<PIP_ADAPTER_INFO>(&adapterInfoBuffer[0]);
    DWORD result = GetAdaptersInfo(pAdapterInfo, &bufferSize);

    if (result == ERROR_BUFFER_OVERFLOW)
    {
        // Need larger buffer
        adapterInfoBuffer.resize(bufferSize);
        pAdapterInfo = reinterpret_cast<PIP_ADAPTER_INFO>(&adapterInfoBuffer[0]);
        result = GetAdaptersInfo(pAdapterInfo, &bufferSize);
    }

    if (result != NO_ERROR)
        R_THROW(("Unable to query network adapters."));

    // Find the first active adapter with a non-zero MAC
    PIP_ADAPTER_INFO pAdapter = pAdapterInfo;
    while (pAdapter)
    {
        // Skip loopback and inactive adapters
        if (pAdapter->Type != MIB_IF_TYPE_LOOPBACK && pAdapter->AddressLength == 6)
        {
            bool isZero = true;
            for (UINT i = 0; i < 6; i++)
            {
                if (pAdapter->Address[i] != 0)
                {
                    isZero = false;
                    break;
                }
            }

            if (!isZero)
            {
                memcpy(&buffer[0], pAdapter->Address, 6);
                return buffer;
            }
        }
        pAdapter = pAdapter->Next;
    }

    // If no valid adapter found, return zeros
#endif
#ifdef IS_LINUX
    int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if(fd < 0)
        R_THROW(("Unable to create datagram socket."));

    struct ifreq s;
    strcpy(s.ifr_name, ifname.c_str());

    if(ioctl(fd, SIOCGIFHWADDR, &s) < 0)
    {
        close(fd);

        R_THROW(("Unable to query MAC address."));
    }

    close(fd);

    memcpy(&buffer[0], &s.ifr_addr.sa_data[0], 6);
#endif
#ifdef IS_MACOS
    // macOS uses getifaddrs to get MAC address
    struct ifaddrs* iflist;
    if (getifaddrs(&iflist) == 0)
    {
        for (struct ifaddrs* cur = iflist; cur; cur = cur->ifa_next)
        {
            if (cur->ifa_addr && cur->ifa_addr->sa_family == AF_LINK)
            {
                // If ifname is empty, find first active non-loopback interface with non-zero MAC
                // Otherwise, match the specific interface name
                if (ifname.empty() || strcmp(cur->ifa_name, ifname.c_str()) == 0)
                {
                    // Skip loopback interface
                    if (cur->ifa_flags & IFF_LOOPBACK)
                        continue;

                    struct sockaddr_dl* sdl = (struct sockaddr_dl*)cur->ifa_addr;
                    unsigned char* mac = (unsigned char*)LLADDR(sdl);

                    // Check if MAC is non-zero
                    bool isZero = true;
                    for (int i = 0; i < 6; i++)
                    {
                        if (mac[i] != 0)
                        {
                            isZero = false;
                            break;
                        }
                    }

                    if (!isZero)
                    {
                        memcpy(&buffer[0], mac, 6);
                        freeifaddrs(iflist);
                        return buffer;
                    }
                }
            }
        }
        freeifaddrs(iflist);
    }
    R_THROW(("Unable to query MAC address."));
#endif
}

string r_utils::r_networking::r_get_device_uuid(const std::string& ifname)
{
    auto hwaddr = r_get_hardware_address(ifname);

    r_md5 h;
    h.update(&hwaddr[0], 6);
    h.finalize();

    vector<uint8_t> buffer(32);
    h.get(&buffer[0]);
    // 6957a8f7-f1ab-4e87-9377-62cea97766f5
    return r_string_utils::format("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                            buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7],
                            buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15]);
}

uint16_t r_utils::r_networking::r_ntohs(uint16_t x)
{
    return ntohs(x);
}

uint16_t r_utils::r_networking::r_htons(uint16_t x)
{
    return htons(x);
}

uint32_t r_utils::r_networking::r_ntohl(uint32_t x)
{
    return ntohl(x);
}

uint32_t r_utils::r_networking::r_htonl(uint32_t x)
{
    return htonl(x);
}

uint64_t r_utils::r_networking::r_ntohll(uint64_t x)
{
    return (((uint64_t) ntohl(x & 0xFFFFFFFF)) << 32LL) + ntohl(x >> 32);
}

uint64_t r_utils::r_networking::r_htonll(uint64_t x)
{
    return (((uint64_t) htonl(x & 0xFFFFFFFF)) << 32LL) + htonl(x >> 32);
}

vector<r_networking::r_adapter_info> r_utils::r_networking::r_get_adapters()
{
    vector<r_adapter_info> adapters;

#ifdef IS_WINDOWS
    ULONG bufferSize = 15000;
    vector<uint8_t> buffer(bufferSize);

    PIP_ADAPTER_ADDRESSES pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(&buffer[0]);

    // GAA_FLAG_INCLUDE_PREFIX is needed for proper address info
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
    DWORD result = GetAdaptersAddresses(AF_INET, flags, nullptr, pAddresses, &bufferSize);

    if (result == ERROR_BUFFER_OVERFLOW)
    {
        buffer.resize(bufferSize);
        pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(&buffer[0]);
        result = GetAdaptersAddresses(AF_INET, flags, nullptr, pAddresses, &bufferSize);
    }

    if (result != NO_ERROR)
        return adapters;

    for (PIP_ADAPTER_ADDRESSES pAdapter = pAddresses; pAdapter != nullptr; pAdapter = pAdapter->Next)
    {
        // Check if adapter is UP
        if (pAdapter->OperStatus != IfOperStatusUp)
            continue;

        // Skip loopback
        if (pAdapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;

        // Check for multicast capability (not NoMulticast)
        if (pAdapter->Flags & IP_ADAPTER_NO_MULTICAST)
            continue;

        // Look for IPv4 unicast addresses
        for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pAdapter->FirstUnicastAddress;
             pUnicast != nullptr;
             pUnicast = pUnicast->Next)
        {
            if (pUnicast->Address.lpSockaddr->sa_family == AF_INET)
            {
                sockaddr_in* sa_in = reinterpret_cast<sockaddr_in*>(pUnicast->Address.lpSockaddr);
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &sa_in->sin_addr, ipStr, INET_ADDRSTRLEN);

                r_adapter_info info;
                // Convert wide string adapter name to narrow string
                int len = WideCharToMultiByte(CP_UTF8, 0, pAdapter->FriendlyName, -1, nullptr, 0, nullptr, nullptr);
                if (len > 0)
                {
                    vector<char> nameBuf(len);
                    WideCharToMultiByte(CP_UTF8, 0, pAdapter->FriendlyName, -1, &nameBuf[0], len, nullptr, nullptr);
                    info.name = &nameBuf[0];
                }
                info.ipv4_addr = ipStr;
                adapters.push_back(info);
            }
        }
    }
#endif

#if defined(IS_LINUX) || defined(IS_MACOS)
    struct ifaddrs* iflist = nullptr;
    if (getifaddrs(&iflist) != 0)
        return adapters;

    for (struct ifaddrs* ifa = iflist; ifa != nullptr; ifa = ifa->ifa_next)
    {
        // Skip if no address
        if (ifa->ifa_addr == nullptr)
            continue;

        // Only interested in IPv4
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        // Check if interface is UP
        if (!(ifa->ifa_flags & IFF_UP))
            continue;

        // Skip loopback
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;

        // Check for multicast capability
        if (!(ifa->ifa_flags & IFF_MULTICAST))
            continue;

        sockaddr_in* sa_in = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sa_in->sin_addr, ipStr, INET_ADDRSTRLEN);

        r_adapter_info info;
        info.name = ifa->ifa_name;
        info.ipv4_addr = ipStr;
        adapters.push_back(info);
    }

    freeifaddrs(iflist);
#endif

    return adapters;
}
