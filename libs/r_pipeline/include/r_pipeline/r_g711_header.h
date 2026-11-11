#ifndef r_pipeline_r_g711_header_h
#define r_pipeline_r_g711_header_h

#include "r_utils/r_macro.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace r_pipeline
{

// Some camera firmware (RTSP server "vp_rtsp_server", HiSilicon-style) starts every G.711 RTP payload with its own
// 4-byte frame header: 00 10 0A 00 ahead of 320 a-law bytes on the ones seen. Played as samples it's a loud click
// every packet (25 a second at 40 ms), and each frame looks 4 samples longer than it is.

constexpr size_t G711_VENDOR_HEADER_SIZE = 4;

// Whether a payload could start with such a header: bytes 0 and 3 are 00 and what follows is whole 20 ms frames
// (a multiple of 160 bytes at 8 kHz), or HiSilicon's 00 01 lo hi with the length in 16-bit words, little-endian,
// exactly the rest.
R_API bool g711_vendor_header_candidate(const uint8_t* data, size_t size);

// Whether one stream's G.711 carries the header, judged on its first packets. One per stream.
//
// - The first packet that could carry one fixes the header's 4 bytes; every packet after must start with the same
//   4 bytes, and could carry one too. Normal G.711 doesn't begin with the same 4 bytes, packet after packet.
// - While judging (the first DETECT_PACKETS), each packet that matches loses its header at once, and the first that
//   doesn't ends it: that stream is never touched again. A normal stream is decided on its first or second packet.
// - When DETECT_PACKETS in a row matched, the stream is stripped from then on.
// - A later packet that doesn't match turns stripping off for good.
class r_g711_header_filter final
{
public:
    enum class state { judging, stripping, plain };

    static constexpr int DETECT_PACKETS = 8;

    // How many leading bytes of this payload to drop (0 or G711_VENDOR_HEADER_SIZE).
    R_API size_t header_to_strip(const uint8_t* data, size_t size);

    R_API state current() const { return _state; }

    // True once, on the packet that made the verdict "stripping" (so a caller logs it once).
    R_API bool just_confirmed() const { return _just_confirmed; }
    // True once, on the packet that turned stripping off after it was confirmed.
    R_API bool just_stopped() const { return _just_stopped; }

private:
    bool _matches(const uint8_t* data, size_t size) const;

    state _state {state::judging};
    int _matched {0};
    std::array<uint8_t, G711_VENDOR_HEADER_SIZE> _header {};
    bool _just_confirmed {false};
    bool _just_stopped {false};
};

}

#endif
