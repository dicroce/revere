
#ifndef r_utils_r_stream_io_h
#define r_utils_r_stream_io_h

#include <stdlib.h>

namespace r_utils
{

class r_stream_io
{
public:
    virtual bool valid() const = 0;
    virtual void send(const void* buf, size_t len) = 0;
    virtual void recv(void* buf, size_t len) = 0;
};

}

#endif
