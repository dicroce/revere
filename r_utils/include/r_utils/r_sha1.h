#ifndef r_utils_r_sha1_h
#define r_utils_r_sha1_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace r_utils
{

class r_sha1 final
{
public:
    R_API r_sha1();
    R_API ~r_sha1();

    R_API void update(const uint8_t* src, size_t size );

    R_API void finalize();

    R_API void get(uint8_t* output);
    R_API std::string get_as_string();

private:
    void _transform(uint32_t* state, const uint8_t* buffer);
    std::array<uint32_t, 5> _state;
    std::array<uint32_t, 2> _count;
    std::array<uint8_t, 64> _buffer;

    bool _finalized;
    std::array<uint8_t, 20> _result;
};

}

#endif
