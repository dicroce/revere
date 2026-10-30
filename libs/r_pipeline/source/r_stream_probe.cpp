#include "r_pipeline/r_stream_probe.h"
#include "r_pipeline/r_gst_source.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_std_utils.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

using namespace r_pipeline;
using namespace r_utils;
using namespace r_utils::r_std_utils;
using namespace std;
using namespace std::chrono;

struct r_stream_tally::state
{
    r_encoding encoding;
    GstH264NalParser* h264 {nullptr};
    GstH265Parser* h265 {nullptr};
    r_stream_probe_result r;
    bool have_sps {false};
    bool have_pts {false};
    int64_t first_pts {0};
    int64_t max_pts {0};
    int64_t last_pts {0};
    bool have_key {false};
    int64_t last_key_pts {0};
};

r_stream_tally::r_stream_tally(r_encoding encoding) :
    _s(make_unique<state>())
{
    _s->encoding = encoding;
    _s->r.encoding = encoding;
    if (encoding == H264_ENCODING)
        _s->h264 = gst_h264_nal_parser_new();
    else if (encoding == H265_ENCODING)
        _s->h265 = gst_h265_parser_new();
}

r_stream_tally::~r_stream_tally() noexcept
{
    if (_s->h264)
        gst_h264_nal_parser_free(_s->h264);
    if (_s->h265)
        gst_h265_parser_free(_s->h265);
}

// The NAL with a start code in front, for parse_h264_sps / parse_h265_sps.
static vector<uint8_t> _with_start_code(const uint8_t* data, size_t offset, size_t size)
{
    vector<uint8_t> out = {0, 0, 0, 1};
    out.insert(out.end(), data + offset, data + offset + size);
    return out;
}

void r_stream_tally::add(const uint8_t* au, size_t size, bool key, int64_t pts_ms)
{
    auto& s = *_s;
    bool picture = false;
    size_t pos = 0;

    if (s.h264)
    {
        GstH264NalUnit nal;
        while (pos < size)
        {
            // The last NAL has no start code after it: NO_NAL_END, and it runs to the end of the access unit.
            auto rc = gst_h264_parser_identify_nalu(s.h264, au, (guint)pos, (guint)size, &nal);
            if (rc != GST_H264_PARSER_OK && rc != GST_H264_PARSER_NO_NAL_END)
                break;
            if (nal.size == 0)
                break;
            if (nal.type == GST_H264_NAL_SPS && !s.have_sps)
            {
                try
                {
                    auto sps = parse_h264_sps(_with_start_code(au, nal.offset, nal.size));
                    s.r.width = sps.width;
                    s.r.height = sps.height;
                    s.r.profile_idc = sps.profile_idc;
                    s.r.level_idc = sps.level_idc;
                    s.r.constraint_flags = sps.constraint_flags;
                    s.have_sps = true;
                }
                catch (...) {}
            }
            if (nal.type == GST_H264_NAL_SLICE || nal.type == GST_H264_NAL_SLICE_IDR)
            {
                picture = true;
                GstH264SliceHdr slice;
                if (gst_h264_parser_parse_slice_hdr(s.h264, &nal, &slice, FALSE, FALSE) == GST_H264_PARSER_OK && GST_H264_IS_B_SLICE(&slice))
                    s.r.b_slices = true;
            }
            else gst_h264_parser_parse_nal(s.h264, &nal);
            pos = nal.offset + nal.size;
        }
    }
    else if (s.h265)
    {
        GstH265NalUnit nal;
        while (pos < size)
        {
            auto rc = gst_h265_parser_identify_nalu(s.h265, au, (guint)pos, (guint)size, &nal);
            if (rc != GST_H265_PARSER_OK && rc != GST_H265_PARSER_NO_NAL_END)
                break;
            if (nal.size == 0)
                break;
            if (nal.type == GST_H265_NAL_SPS && !s.have_sps)
            {
                try
                {
                    auto sps = parse_h265_sps(_with_start_code(au, nal.offset, nal.size));
                    s.r.width = sps.width;
                    s.r.height = sps.height;
                    s.r.profile_idc = sps.profile_idc;
                    s.r.level_idc = sps.level_idc;
                    s.have_sps = true;
                }
                catch (...) {}
            }
            // VCL NAL units (slices) are types 0 to 31.
            if (nal.type <= 31)
            {
                picture = true;
                GstH265SliceHdr slice;
                if (gst_h265_parser_parse_slice_hdr(s.h265, &nal, &slice) == GST_H265_PARSER_OK && GST_H265_IS_B_SLICE(&slice))
                    s.r.b_slices = true;
            }
            else gst_h265_parser_parse_nal(s.h265, &nal);
            pos = nal.offset + nal.size;
        }
    }

    if (!picture)
        return;

    ++s.r.frames;
    if (s.have_pts && pts_ms < s.last_pts)
        s.r.b_slices = true;  // displayed before a picture decoded ahead of it
    if (!s.have_pts)
    {
        s.first_pts = pts_ms;
        s.max_pts = pts_ms;
        s.have_pts = true;
    }
    s.max_pts = max(s.max_pts, pts_ms);
    s.last_pts = pts_ms;

    if (key)
    {
        ++s.r.key_frames;
        if (s.have_key)
            s.r.key_interval_ms = max(s.r.key_interval_ms, pts_ms - s.last_key_pts);
        s.have_key = true;
        s.last_key_pts = pts_ms;
    }
}

r_stream_probe_result r_stream_tally::result() const
{
    auto r = _s->r;
    if (r.frames >= 2 && _s->max_pts > _s->first_pts)
        r.fps = (r.frames - 1) * 1000.0 / (double)(_s->max_pts - _s->first_pts);
    return r;
}

static r_stream_probe_result _probe_stream(
    const string& rtsp_url,
    const r_nullable<string>& username,
    const r_nullable<string>& password,
    int min_seconds,
    int max_seconds,
    const string& protocols
)
{
    vector<r_arg> arguments;
    add_argument(arguments, "url", rtsp_url);
    add_argument(arguments, "protocols", protocols);
    if (!username.is_null())
        add_argument(arguments, "username", username.value());
    if (!password.is_null())
        add_argument(arguments, "password", password.value());

    r_gst_source src;
    src.set_args(arguments);

    mutex lock;
    unique_ptr<r_stream_tally> tally;
    atomic<bool> answered {false};
    atomic<int> key_frames {0};

    src.set_sdp_media_cb([&](const map<string, r_sdp_media>&) { answered = true; });
    src.set_video_sample_cb([&](const sample_context& sc, const r_gst_buffer& buffer, bool key, int64_t pts) {
        lock_guard<mutex> g(lock);
        if (!tally)
        {
            auto encoding = sc.video_encoding();
            if (encoding.is_null() || (encoding.value() != H264_ENCODING && encoding.value() != H265_ENCODING))
                return;
            tally = make_unique<r_stream_tally>(encoding.value());
        }
        auto mi = buffer.map(r_gst_buffer::MT_READ);
        tally->add(mi.data(), mi.size(), key, pts);
        if (key)
            ++key_frames;
    });

    src.play();
    auto started = steady_clock::now();
    while (true)
    {
        auto elapsed = steady_clock::now() - started;
        if (elapsed >= seconds(max_seconds) || (elapsed >= seconds(min_seconds) && key_frames >= 2))
            break;
        this_thread::sleep_for(milliseconds(100));
    }
    src.stop();

    lock_guard<mutex> g(lock);
    r_stream_probe_result r = tally ? tally->result() : r_stream_probe_result{};
    r.answered = answered;
    r.received_video = tally && r.frames > 0;
    return r;
}

r_stream_probe_result r_pipeline::probe_stream(
    const string& rtsp_url,
    const r_nullable<string>& username,
    const r_nullable<string>& password,
    int min_seconds,
    int max_seconds
)
{
    auto r = _probe_stream(rtsp_url, username, password, min_seconds, max_seconds, "TCP");
    if (rtsp_probe_retry_over_udp(r.answered, r.received_video))
    {
        R_LOG_WARNING("Camera answered but sent no video over RTSP/TCP; probing over UDP");
        r = _probe_stream(rtsp_url, username, password, min_seconds, max_seconds, "UDP");
    }
    return r;
}

r_substream_check r_pipeline::check_substream(const r_stream_probe_result& sub, uint16_t main_width, uint16_t main_height)
{
    r_substream_check c;
    if (sub.width > 0 && sub.height > 0 && main_width > 0 && main_height > 0)
    {
        double main_aspect = (double)main_width / main_height;
        double sub_aspect = (double)sub.width / sub.height;
        c.anamorphic = fabs(sub_aspect - main_aspect) / main_aspect > SUBSTREAM_ANAMORPHIC_TOLERANCE;
    }

    if (!sub.answered)
        c.reason = "camera didn't answer";
    else if (!sub.received_video)
        c.reason = "no video";
    else if (sub.encoding != H264_ENCODING && sub.encoding != H265_ENCODING)
        c.reason = "not H.264 or H.265";
    else if (sub.width == 0 || sub.height == 0)
        c.reason = "no stream size";
    else if ((int64_t)sub.width * sub.height * 4 > (int64_t)main_width * main_height)
        c.reason = "too large";
    else if (sub.b_slices)
        c.reason = "uses B-frames";
    else if (sub.key_interval_ms < 0 || sub.key_interval_ms > SUBSTREAM_MAX_KEY_INTERVAL_MS)
        c.reason = "keyframes too far apart";
    else if (sub.fps < SUBSTREAM_MIN_FPS)
        c.reason = "frame rate too low";
    else c.ok = true;
    return c;
}
