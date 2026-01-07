
#include "r_utils/r_udp_socket.h"

using namespace r_utils;
using namespace std;

r_udp_socket::r_udp_socket() :
    _sok((sock_t)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))
{
    if(_sok < 0)
        R_THROW(("Unable to create datagram socket."));
}

r_udp_socket::r_udp_socket(r_udp_socket&& obj) noexcept :
    _sok(move(obj._sok))
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
        obj._sok = kInvalidSock;
    }
    return *this;
}

int r_udp_socket::sendto(const uint8_t* buffer, size_t size, r_socket_address& address)
{
#ifdef IS_WINDOWS
    return ::sendto((sock_t)_sok, (char*)buffer, (int)size, 0, address.get_sock_addr(), (int)address.sock_addr_size());
#endif
#ifdef IS_LINUX
    return ::sendto((sock_t)_sok, buffer, size, 0, address.get_sock_addr(), address.sock_addr_size());
#endif
}

int r_udp_socket::recvfrom(uint8_t* buffer, size_t size, r_socket_address& address)
{
#ifdef IS_WINDOWS
    socklen_t addr_len = address.sock_addr_size();
    return ::recvfrom((sock_t)_sok, (char*)buffer, (int)size, 0, address.get_sock_addr(), &addr_len);
#endif
#ifdef IS_LINUX
    socklen_t addr_len = address.sock_addr_size();
    return ::recvfrom((sock_t)_sok, buffer, size, 0, address.get_sock_addr(), &addr_len);
#endif
}

void r_udp_socket::_clear() noexcept
{
    if(_sok != kInvalidSock)
    {
#ifdef IS_LINUX
        ::shutdown(_sok, SHUT_RDWR);
        ::close(_sok);
#endif
#ifdef IS_WINDOWS
        ::shutdown(_sok, SD_BOTH);
        ::closesocket(_sok);
#endif
    }
}
