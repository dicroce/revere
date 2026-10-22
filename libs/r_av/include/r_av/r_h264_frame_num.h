#ifndef r_av_r_h264_frame_num_h
#define r_av_r_h264_frame_num_h

#include "r_utils/r_macro.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace r_av
{

// Follows frame_num across an H.264 Annex B stream, one access unit at a time,
// to spot pictures that never arrived (RTP loss drops whole frames: the
// depayloader discards an incomplete one). A gap matters on decoders that keep
// their own reference state (VideoToolbox): they predict the frames after it
// from a stale surface, so pictures from seconds earlier come back.
//
// Reads just enough of the SPS (log2_max_frame_num, gaps_in_frame_num_allowed),
// the PPS (which SPS) and each picture's first slice header (frame_num,
// nal_ref_idc). Multi-slice pictures count once; non-reference pictures share
// the frame_num after the last reference one; frame_num wraps. Not handled: an
// MMCO 5 (it resets frame_num, so the next picture reads as a gap; cameras
// don't send it).
class r_h264_frame_num_tracker final
{
public:
    enum result
    {
        NO_PICTURE,  // no slice of a new picture here (parameter sets, SEI, or a later slice)
        UNKNOWN,     // a picture, but no IDR or SPS seen yet to check it against
        IN_ORDER,
        GAP,         // at least one reference picture is missing before this one
        IDR
    };

    R_API result feed(const uint8_t* data, size_t size);
    R_API void reset();

    // The last SPS and PPS of each id seen, as Annex B (SPSs first).
    R_API std::vector<uint8_t> parameter_sets() const;

private:
    struct sps_info { int log2_max_frame_num {4}; bool separate_colour_plane {false}; bool gaps_allowed {false}; };

    result _slice(const uint8_t* nal, size_t size);
    void _sps(const uint8_t* nal, size_t size);
    void _pps(const uint8_t* nal, size_t size);

    std::map<uint32_t, sps_info> _sps_by_id;
    std::map<uint32_t, uint32_t> _sps_of_pps;
    std::map<uint32_t, std::vector<uint8_t>> _sps_nal;
    std::map<uint32_t, std::vector<uint8_t>> _pps_nal;
    bool _have_ref {false};
    uint32_t _last_ref_frame_num {0};
};

}

#endif
