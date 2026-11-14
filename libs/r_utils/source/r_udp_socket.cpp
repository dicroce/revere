#include "r_utils/r_udp_socket.h"
#include <cerrno>

using namespace r_utils;
using namespace std;
using namespace std::chrono;

static void _set_timeout(sock_t sok, int opt, milliseconds timeout)
{
#ifdef IS_WINDOWS
    DWORD t = (DWORD)timeout.count();
    ::setsockopt(sok, SOL_SOCKET, opt, (const char*)&t, sizeof(t));
#else
    struct timeval t;
    t.tv_sec = (time_t)(timeout.count() / 1000);
    t.tv_usec = (suseconds_t)((timeout.count() % 1000) * 1000);
    ::setsockopt(sok, SOL_SOCKET, opt, &t, sizeof(t));
#endif
}

static struct in_addr _ipv4(const string& ip)
{
    struct in_addr a;
    memset(&a, 0, sizeof(a));
    if(inet_pton(AF_INET, ip.c_str(), &a) != 1)
        R_THROW(("Not an IPv4 address: %s", ip.c_str()));
    return a;
}

r_udp_socket::r_udp_socket() :
    _sok((sock_t)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))
{
    if(_sok == kInvalidSock)
        R_THROW(("Unable to create datagram socket."));
}

r_udp_socket::r_udp_socket(r_udp_socket&& obj) noexcept :
    _sok(move(obj._sok)),
    _timed_out(obj._timed_out)
{
    obj._sok = kInvalidSock;
}

r_udp_socket::~r_udp_socket() noexcept
{
    _clear();
}

r_udp_socket& r_udp_socket::operator=(r_udp_socket&& obj) noexcept
{
    if(this != &obj)
    {
        _clear();
        _sok = move(obj._sok);
        _timed_out = obj._timed_out;
        obj._sok = kInvalidSock;
    }
    return *this;
}

void r_udp_socket::bind(int port, const string& addr)
{
    r_socket_address a(port, addr);
    if(::bind(_sok, a.get_sock_addr(), a.sock_addr_size()) != 0)
        R_THROW(("Unable to bind datagram socket to %s:%d.", addr.c_str(), port));
}

void r_udp_socket::set_reuse_addr(bool on)
{
    int v = on ? 1 : 0;
    ::setsockopt(_sok, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, sizeof(v));
}

void r_udp_socket::set_recv_timeout(milliseconds timeout)
{
    _set_timeout(_sok, SO_RCVTIMEO, timeout);
}

void r_udp_socket::set_send_timeout(milliseconds timeout)
{
    _set_timeout(_sok, SO_SNDTIMEO, timeout);
}

void r_udp_socket::set_multicast_interface(const string& iface_ip)
{
    auto iface = _ipv4(iface_ip);
    ::setsockopt(_sok, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&iface, sizeof(iface));
}

void r_udp_socket::join_multicast_group(const string& group, const string& iface_ip)
{
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr = _ipv4(group);
    mreq.imr_interface = _ipv4(iface_ip);
    ::setsockopt(_sok, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
}

void r_udp_socket::set_multicast_ttl(int ttl)
{
#ifdef IS_WINDOWS
    DWORD v = (DWORD)ttl;
#else
    unsigned char v = (unsigned char)ttl;
#endif
    ::setsockopt(_sok, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&v, sizeof(v));
}

int r_udp_socket::sendto(const uint8_t* buffer, size_t size, r_socket_address& address, bool dont_wait)
{
#ifdef IS_WINDOWS
    (void)dont_wait;
    return ::sendto((sock_t)_sok, (char*)buffer, (int)size, 0, address.get_sock_addr(), (int)address.sock_addr_size());
#else
    return (int)::sendto((sock_t)_sok, buffer, size, dont_wait ? MSG_DONTWAIT : 0, address.get_sock_addr(), address.sock_addr_size());
#endif
}

int r_udp_socket::recvfrom(uint8_t* buffer, size_t size, r_socket_address& address)
{
    _timed_out = false;
    struct sockaddr_storage from;
    memset(&from, 0, sizeof(from));
    socklen_t addr_len = sizeof(from);
#ifdef IS_WINDOWS
    int n = ::recvfrom((sock_t)_sok, (char*)buffer, (int)size, 0, (struct sockaddr*)&from, &addr_len);
    if(n < 0)
    {
        _timed_out = (WSAGetLastError() == WSAETIMEDOUT);
        return n;
    }
#else
    int n = (int)::recvfrom((sock_t)_sok, buffer, size, 0, (struct sockaddr*)&from, &addr_len);
    if(n < 0)
    {
        _timed_out = (errno == EAGAIN || errno == EWOULDBLOCK);
        return n;
    }
#endif
    address = r_socket_address((struct sockaddr*)&from, addr_len);
    return n;
}

void r_udp_socket::_clear() noexcept
{
    if(_sok != kInvalidSock)
    {
#if defined(IS_LINUX) || defined(IS_MACOS)
        ::shutdown(_sok, SHUT_RDWR);
        ::close(_sok);
#endif
#ifdef IS_WINDOWS
        ::shutdown(_sok, SD_BOTH);
        ::closesocket(_sok);
#endif
        _sok = kInvalidSock;
    }
}
