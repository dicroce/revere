
#ifndef r_utils_r_timer_h
#define r_utils_r_timer_h

#include "r_utils/r_socket_address.h"
#include "r_utils/r_socket.h"

namespace r_utils
{

class r_udp_socket
{
public:
    R_API r_udp_socket();
    R_API r_udp_socket(const r_udp_socket&) = delete;
    R_API r_udp_socket(r_udp_socket&& obj) noexcept;
    R_API ~r_udp_socket() noexcept;

    R_API r_udp_socket& operator = (const r_udp_socket& obj);
    R_API r_udp_socket& operator = (r_udp_socket&& obj) noexcept;

    R_API int sendto(const uint8_t* buffer, size_t size, r_socket_address& address);
    R_API int recvfrom(uint8_t* buffer, size_t size, r_socket_address& address);

    R_API sock_t fd() const {return _sok;}

private:
    void _clear() noexcept;
    sock_t _sok;
};

}

#endif
