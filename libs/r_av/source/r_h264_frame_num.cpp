
#include "r_av/r_h264_frame_num.h"
#include <vector>

using namespace r_av;
using namespace std;

namespace
{

// Exp-Golomb reader over an RBSP (emulation prevention bytes already removed).
// Reads past the end yield zeros and set overrun.
class bit_reader
{
public:
    bit_reader(const vector<uint8_t>& rbsp) : _d(rbsp) {}

    uint32_t u(int n)
    {
        uint32_t v = 0;
        for(int i = 0; i < n; ++i)
        {
            v <<= 1;
            if(_bit < _d.size() * 8)
                v |= (_d[_bit / 8] >> (7 - (_bit % 8))) & 1;
            else overrun = true;
            ++_bit;
        }
        return v;
    }

    uint32_t ue()
    {
        int zeros = 0;
        while(u(1) == 0)
        {
            if(overrun || ++zeros > 31)
            {
                overrun = true;
                return 0;
            }
        }
        return zeros == 0 ? 0 : ((1u << zeros) - 1) + u(zeros);
    }

    int32_t se()
    {
        auto k = ue();
        return (k & 1) ? (int32_t)((k + 1) / 2) : -(int32_t)(k / 2);
    }

    bool overrun {false};

private:
    const vector<uint8_t>& _d;
    size_t _bit {0};
};

// The NAL payload after its one-byte header, without emulation prevention bytes.
// Only the first `max_bytes` are needed for the headers read here.
vector<uint8_t> rbsp_of(const uint8_t* nal, size_t size, size_t max_bytes)
{
    vector<uint8_t> out;
    out.reserve(min(size, max_bytes));
    int zeros = 0;
    for(size_t i = 1; i < size && out.size() < max_bytes; ++i)
    {
        if(zeros >= 2 && nal[i] == 3)
        {
            zeros = 0;
            continue;
        }
        zeros = (nal[i] == 0) ? zeros + 1 : 0;
        out.push_back(nal[i]);
    }
    return out;
}

void skip_scaling_list(bit_reader& br, int size)
{
    int last = 8, next = 8;
    for(int j = 0; j < size && !br.overrun; ++j)
    {
        if(next != 0)
            next = (last + br.se() + 256) % 256;
        last = (next == 0) ? last : next;
    }
}

}

void r_h264_frame_num_tracker::reset()
{
    _have_ref = false;
}

vector<uint8_t> r_h264_frame_num_tracker::parameter_sets() const
{
    vector<uint8_t> out;
    for(auto* m : {&_sps_nal, &_pps_nal})
        for(auto& kv : *m)
        {
            out.insert(out.end(), {0, 0, 0, 1});
            out.insert(out.end(), kv.second.begin(), kv.second.end());
        }
    return out;
}

r_h264_frame_num_tracker::result r_h264_frame_num_tracker::feed(const uint8_t* data, size_t size)
{
    // Walk the Annex B start codes; each NAL runs to the next one.
    vector<pair<size_t, size_t>> nals;
    size_t i = 0, start = SIZE_MAX;
    while(i + 2 < size)
    {
        if(data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
        {
            if(start != SIZE_MAX)
            {
                size_t end = i;
                while(end > start && data[end - 1] == 0)
                    --end;  // trailing zeros belong to the next start code
                nals.push_back({start, end - start});
            }
            i += 3;
            start = i;
        }
        else ++i;
    }
    if(start != SIZE_MAX && start < size)
        nals.push_back({start, size - start});

    result res = NO_PICTURE;
    for(auto& n : nals)
    {
        auto nal = data + n.first;
        auto len = n.second;
        if(len < 2)
            continue;
        auto type = nal[0] & 0x1f;
        if(type == 7)
            _sps(nal, len);
        else if(type == 8)
            _pps(nal, len);
        else if(type == 1 || type == 5)
        {
            auto r = _slice(nal, len);
            if(r == IDR || r == GAP)
                res = r;
            else if(res == NO_PICTURE || res == UNKNOWN)
                res = r;
        }
    }
    return res;
}

void r_h264_frame_num_tracker::_sps(const uint8_t* nal, size_t size)
{
    auto rbsp = rbsp_of(nal, size, 1024);
    bit_reader br(rbsp);
    auto profile_idc = br.u(8);
    br.u(16);  // constraint flags, level_idc
    auto sps_id = br.ue();
    sps_info s;
    if(profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 || profile_idc == 44 ||
       profile_idc == 83 || profile_idc == 86 || profile_idc == 118 || profile_idc == 128 || profile_idc == 138 ||
       profile_idc == 139 || profile_idc == 134 || profile_idc == 135)
    {
        auto chroma_format_idc = br.ue();
        if(chroma_format_idc == 3)
            s.separate_colour_plane = br.u(1) != 0;
        br.ue();  // bit_depth_luma_minus8
        br.ue();  // bit_depth_chroma_minus8
        br.u(1);  // qpprime_y_zero_transform_bypass_flag
        if(br.u(1))  // seq_scaling_matrix_present_flag
        {
            int lists = (chroma_format_idc != 3) ? 8 : 12;
            for(int l = 0; l < lists; ++l)
                if(br.u(1))
                    skip_scaling_list(br, l < 6 ? 16 : 64);
        }
    }
    s.log2_max_frame_num = (int)br.ue() + 4;
    auto poc_type = br.ue();
    if(poc_type == 0)
        br.ue();  // log2_max_pic_order_cnt_lsb_minus4
    else if(poc_type == 1)
    {
        br.u(1);  // delta_pic_order_always_zero_flag
        br.se();
        br.se();
        auto cycle = br.ue();
        for(uint32_t c = 0; c < cycle && c < 256 && !br.overrun; ++c)
            br.se();
    }
    br.ue();  // max_num_ref_frames
    s.gaps_allowed = br.u(1) != 0;

    if(br.overrun || sps_id > 31 || s.log2_max_frame_num > 16)
        return;  // unreadable: keep what we had
    auto it = _sps_by_id.find(sps_id);
    if(it != _sps_by_id.end() && it->second.log2_max_frame_num != s.log2_max_frame_num)
        _have_ref = false;  // a new stream layout: wait for its IDR before judging
    _sps_by_id[sps_id] = s;
    _sps_nal[sps_id].assign(nal, nal + size);
}

void r_h264_frame_num_tracker::_pps(const uint8_t* nal, size_t size)
{
    auto rbsp = rbsp_of(nal, size, 16);
    bit_reader br(rbsp);
    auto pps_id = br.ue();
    auto sps_id = br.ue();
    if(!br.overrun && pps_id <= 255 && sps_id <= 31)
    {
        _sps_of_pps[pps_id] = sps_id;
        _pps_nal[pps_id].assign(nal, nal + size);
    }
}

r_h264_frame_num_tracker::result r_h264_frame_num_tracker::_slice(const uint8_t* nal, size_t size)
{
    bool idr = (nal[0] & 0x1f) == 5;
    bool ref = (nal[0] & 0x60) != 0;  // nal_ref_idc

    auto rbsp = rbsp_of(nal, size, 32);
    bit_reader br(rbsp);
    auto first_mb = br.ue();
    br.ue();  // slice_type
    auto pps_id = br.ue();
    if(br.overrun || first_mb != 0)
        return NO_PICTURE;  // a later slice of a picture already counted

    auto p = _sps_of_pps.find(pps_id);
    if(p == _sps_of_pps.end())
        return UNKNOWN;
    auto s = _sps_by_id.find(p->second);
    if(s == _sps_by_id.end())
        return UNKNOWN;
    if(s->second.separate_colour_plane)
        br.u(2);  // colour_plane_id
    auto frame_num = br.u(s->second.log2_max_frame_num);
    if(br.overrun)
        return UNKNOWN;

    if(idr)
    {
        _have_ref = true;
        _last_ref_frame_num = frame_num;
        return IDR;
    }
    if(!_have_ref)
        return UNKNOWN;

    // Every picture after a reference picture carries its frame_num + 1;
    // non-reference pictures don't advance it.
    uint32_t max_frame_num = 1u << s->second.log2_max_frame_num;
    bool in_order = frame_num == (_last_ref_frame_num + 1) % max_frame_num;
    if(ref)
        _last_ref_frame_num = frame_num;
    return (in_order || s->second.gaps_allowed) ? IN_ORDER : GAP;
}
