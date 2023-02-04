
#ifndef r_utils_r_pollable_h
#define r_utils_r_pollable_h

#include <cstdint>

namespace r_utils
{

class r_pollable
{
public:
    virtual bool wait_till_recv_wont_block(uint64_t& millis) const = 0;
    virtual bool wait_till_send_wont_block(uint64_t& millis) const = 0;
};

}

#endif
