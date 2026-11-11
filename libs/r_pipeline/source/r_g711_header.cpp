#include "r_pipeline/r_g711_header.h"
#include <algorithm>

using namespace r_pipeline;

bool r_pipeline::g711_vendor_header_candidate(const uint8_t* data, size_t size)
{
    if(!data || size <= G711_VENDOR_HEADER_SIZE)
        return false;
    const size_t rest = size - G711_VENDOR_HEADER_SIZE;
    // HiSilicon's frame header: 00 01, the length in 16-bit words, little-endian.
    if(data[0] == 0x00 && data[1] == 0x01 && ((size_t)data[2] | ((size_t)data[3] << 8)) * 2 == rest)
        return true;
    // 00 ?? ?? 00 ahead of whole 20 ms frames (as 00 10 0A 00 ahead of 320 bytes).
    return data[0] == 0x00 && data[3] == 0x00 && rest % 160 == 0;
}

bool r_g711_header_filter::_matches(const uint8_t* data, size_t size) const
{
    return g711_vendor_header_candidate(data, size) && std::equal(_header.begin(), _header.end(), data);
}

size_t r_g711_header_filter::header_to_strip(const uint8_t* data, size_t size)
{
    _just_confirmed = false;
    _just_paused = false;
    _just_stopped = false;

    switch(_state)
    {
        case state::plain:
            return 0;
        case state::judging:
            if(_matched == 0)
            {
                if(!g711_vendor_header_candidate(data, size))
                {
                    _state = state::plain;
                    return 0;
                }
                std::copy(data, data + G711_VENDOR_HEADER_SIZE, _header.begin());
            }
            else if(!_matches(data, size))
            {
                _state = state::plain;
                return 0;
            }
            if(++_matched >= DETECT_PACKETS)
            {
                _state = state::stripping;
                _just_confirmed = true;
            }
            return G711_VENDOR_HEADER_SIZE;
        case state::stripping:
            if(!_matches(data, size))
            {
                // One odd packet: left as it is, and the stream is judged again.
                _state = state::rejudging;
                _matched = 0;
                _just_paused = true;
                return 0;
            }
            return G711_VENDOR_HEADER_SIZE;
        case state::rejudging:
            if(!_matches(data, size))
            {
                _state = state::plain;
                _just_stopped = true;
                return 0;
            }
            if(++_matched >= DETECT_PACKETS)
            {
                _state = state::stripping;
                _just_confirmed = true;
                ++_resumes;
            }
            return G711_VENDOR_HEADER_SIZE;
    }
    return 0;
}
