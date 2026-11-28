
#ifndef r_utils_r_socket_base_h
#define r_utils_r_socket_base_h

#include "r_utils/r_macro.h"
#include <stdlib.h>
#include <string>
#include <cstdint>

namespace r_utils
{

class r_socket_base
{
public:
    R_API virtual ~r_socket_base() noexcept {}
    R_API virtual void connect(const std::string& host, int port) = 0;
    R_API virtual void close() = 0;
    // Wakes whoever is blocked on the socket, from another thread, and releases nothing: the TCP connection is shut
    // down (its reads and writes fail from then on), but the socket and any TLS state stay as they are, for the thread
    // using it to close. Unlike close() from another thread, it can't race a read inside TLS, or free a descriptor
    // still in use. The default does nothing (a socket with no descriptor of its own).
    R_API virtual void shutdown() {}
    R_API virtual bool valid() const = 0;
    R_API virtual int send(const void* buf, size_t len) = 0;
    R_API virtual int recv(void* buf, size_t len) = 0;
    R_API virtual bool wait_till_recv_wont_block(uint64_t& millis) const = 0;
    R_API virtual bool wait_till_send_wont_block(uint64_t& millis) const = 0;
    // True once the peer has closed (or the connection has failed), without
    // blocking or consuming data; lets a handler stop working for a client
    // that has gone away.
    R_API virtual bool peer_closed() const = 0;
    // The remote address as text ("192.168.1.20", "::1"); throws if not connected.
    R_API virtual std::string get_peer_ip() const = 0;
};

}

#endif
