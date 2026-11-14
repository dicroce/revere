
#ifndef r_utils_r_udp_socket_h
#define r_utils_r_udp_socket_h

#include "r_utils/r_socket_address.h"
#include "r_utils/r_socket.h"
#include <chrono>

namespace r_utils
{

// An IPv4 datagram socket.
class r_udp_socket
{
public:
    R_API r_udp_socket();
    R_API r_udp_socket(const r_udp_socket&) = delete;
    R_API r_udp_socket(r_udp_socket&& obj) noexcept;
    R_API ~r_udp_socket() noexcept;

    R_API r_udp_socket& operator = (const r_udp_socket& obj);
    R_API r_udp_socket& operator = (r_udp_socket&& obj) noexcept;

    // Binds to addr:port; port 0 takes any free port. Throws if it can't.
    R_API void bind(int port = 0, const std::string& addr = ip4_addr_any);

    R_API void set_reuse_addr(bool on);

    // recvfrom() gives up after timeout, returning -1 with timed_out() true.
    R_API void set_recv_timeout(std::chrono::milliseconds timeout);

    // A blocked sendto() gives up after timeout.
    R_API void set_send_timeout(std::chrono::milliseconds timeout);

    // Multicast sends go out through the interface with this IPv4 address.
    R_API void set_multicast_interface(const std::string& iface_ip);

    // Joins an IPv4 multicast group on the interface with iface_ip.
    R_API void join_multicast_group(const std::string& group, const std::string& iface_ip);

    R_API void set_multicast_ttl(int ttl);

    // dont_wait: fail rather than block when the send can't go out at once (POSIX
    // MSG_DONTWAIT). Windows has no per-call flag; there set_send_timeout() is
    // the limit.
    R_API int sendto(const uint8_t* buffer, size_t size, r_socket_address& address, bool dont_wait = false);

    // On success address is the sender's.
    R_API int recvfrom(uint8_t* buffer, size_t size, r_socket_address& address);

    // True when the last recvfrom() failed because its timeout ran out.
    R_API bool timed_out() const {return _timed_out;}

    R_API sock_t fd() const {return _sok;}

private:
    void _clear() noexcept;
    sock_t _sok;
    bool _timed_out {false};
};

}

#endif
