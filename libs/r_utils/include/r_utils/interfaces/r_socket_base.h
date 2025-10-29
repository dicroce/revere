
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
    R_API virtual void close() const = 0;
    R_API virtual bool valid() const = 0;
    R_API virtual int send(const void* buf, size_t len) = 0;
    R_API virtual int recv(void* buf, size_t len) = 0;
    R_API virtual bool wait_till_recv_wont_block(uint64_t& millis) const = 0;
    R_API virtual bool wait_till_send_wont_block(uint64_t& millis) const = 0;
};

}

#endif
