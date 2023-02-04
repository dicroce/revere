
#ifndef r_utils_r_socket_io_h
#define r_utils_r_socket_io_h

#include <stdlib.h>

namespace r_utils
{

class r_socket_io
{
public:
    virtual int raw_send(const void* buf, size_t len) = 0;
    virtual int raw_recv(void* buf, size_t len) = 0;
};

}

#endif
