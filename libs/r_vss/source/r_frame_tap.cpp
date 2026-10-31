#include "r_vss/r_frame_tap.h"
#include "r_av/r_video_decoder.h"
#include "r_av/r_video_encoder.h"
#include "r_av/r_muxer.h"
#include "r_pipeline/r_stream_info.h"
#include "r_utils/r_logger.h"
#include <cstring>

using namespace r_vss;
using namespace r_av;
using namespace std;
using namespace std::chrono;

namespace
{

// Scaled so the longer edge is long_edge (never up), both even for YUV 4:2:0.
void _fit(uint16_t in_w, uint16_t in_h, uint16_t long_edge, uint16_t& w, uint16_t& h)
{
    double scale = 1.0;
    auto longer = in_w > in_h ? in_w : in_h;
    if(longer > long_edge && longer > 0)
        scale = (double)long_edge / (double)longer;
    w = (uint16_t)(in_w * scale);
    h = (uint16_t)(in_h * scale);
    w = (uint16_t)(w & ~1u);
    h = (uint16_t)(h & ~1u);
    if(w < 2) w = 2;
    if(h < 2) h = 2;
}

int64_t _steady_ms()
{
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// A decode call that takes no input, or keeps giving output, can't loop for ever.
constexpr int MAX_DECODE_CALLS = 16;

}

r_frame_tap::r_frame_tap(uint16_t long_edge, extradata_fn extradata) :
    _extradata(extradata ? std::move(extradata) : extradata_fn([](const string& name, const string& params) {
        return params.empty() ? vector<uint8_t>() : r_pipeline::get_video_codec_extradata(name, params);
    })),
    _long_edge(long_edge),
    _th()
{
    _th = thread(&r_frame_tap::_entry_point, this);
}

r_frame_tap::~r_frame_tap() noexcept
{
    {
        lock_guard<mutex> g(_q_lock);
        _running = false;
    }
    _q_cond.notify_one();
    if(_th.joinable())
        _th.join();
}

void r_frame_tap::post(const uint8_t* data, size_t size, bool key, int64_t ts, const string& codec_name, const string& codec_params)
{
    if(!data || size == 0)
        return;
    {
        lock_guard<mutex> g(_q_lock);
        // A decoder that fell behind starts again at the next key frame: the
        // picture is meant to be now, not a catch-up through old frames.
        if(_q.size() >= MAX_QUEUED)
        {
            _q.clear();
            _need_key = true;
            ++_queue_resets;
        }
        // Frames before a key frame can't be decoded on their own.
        if(_need_key)
        {
            if(!key)
                return;
            _need_key = false;
        }
        _frame f;
        f.data.assign(data, data + size);
        f.key = key;
        f.ts = ts;
        f.codec_name = codec_name;
        f.codec_params = codec_params;
        _q.push_back(std::move(f));
    }
    _q_cond.notify_one();
}

void r_frame_tap::_entry_point()
{
    unique_ptr<r_video_decoder> decoder;
    string codec_name, codec_params;
    int64_t last_picture_ms = 0;
    bool any_picture = false;

    while(true)
    {
        _frame f;
        {
            unique_lock<mutex> lk(_q_lock);
            _q_cond.wait(lk, [this]() { return !_running || !_q.empty(); });
            if(!_running)
                return;
            f = std::move(_q.front());
            _q.pop_front();
        }

        try
        {
            // A new stream (or the first): a new decoder, from a key frame.
            if(!decoder || f.codec_name != codec_name || f.codec_params != codec_params)
            {
                decoder.reset();
                if(!f.key)
                    continue;
                // Each posted frame is one whole access unit, so no parser: with one,
                // a frame comes out only when the next arrives, a frame behind.
                decoder = make_unique<r_video_decoder>(encoding_to_av_codec_id(f.codec_name), r_hw_accel::none, false);
                // A picture is wanted now, not a frame per decoder thread later.
                decoder->set_low_latency();
                auto ed = _extradata(f.codec_name, f.codec_params);
                if(!ed.empty())
                    decoder->set_extradata(ed);
                codec_name = f.codec_name;
                codec_params = f.codec_params;
            }

            decoder->attach_buffer(f.data.data(), f.data.size());
            for(int calls = 0; calls < MAX_DECODE_CALLS; ++calls)
            {
                auto ds = decoder->decode();
                if(ds != R_CODEC_STATE_HAS_OUTPUT && ds != R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                    break;
                ++_frames_decoded;

                // Every frame is decoded (each needs the one before), but a
                // picture is made from one at most every PICTURE_EVERY_MS.
                auto now_ms = _steady_ms();
                if(any_picture && now_ms - last_picture_ms < PICTURE_EVERY_MS)
                    continue;

                uint16_t w = 0, h = 0;
                _fit(decoder->input_width(), decoder->input_height(), _long_edge.load(), w, h);
                auto yuv = decoder->get(AV_PIX_FMT_YUVJ420P, w, h, 1);
                if(!yuv)
                    continue;
                {
                    lock_guard<mutex> g(_picture_lock);
                    // The posted frame's time. With B-frames the picture that comes
                    // out can be a frame or two older; IP cameras rarely send them.
                    _newest.ts = f.ts;
                    _newest.w = w;
                    _newest.h = h;
                    _newest.yuv = yuv;
                }
                last_picture_ms = now_ms;
                any_picture = true;
                ++_pictures_made;
            }
        }
        catch(const exception& ex)
        {
            // Start again at the next key frame with a new decoder.
            R_LOG_ERROR("frame tap: decode failed: %s", ex.what());
            decoder.reset();
            lock_guard<mutex> g(_q_lock);
            _q.clear();
            _need_key = true;
        }
    }
}

bool r_frame_tap::newest_jpeg(int64_t after, uint16_t long_edge, vector<uint8_t>& jpeg, int64_t& ts)
{
    _long_edge.store(long_edge);

    _picture p;
    {
        lock_guard<mutex> g(_picture_lock);
        if(!_newest.yuv || _newest.ts <= after)
            return false;
        // Made already for an earlier ask of the same frame.
        if(_jpeg_ts == _newest.ts && !_jpeg.empty())
        {
            jpeg = _jpeg;
            ts = _jpeg_ts;
            return true;
        }
        p = _newest;
    }

    r_video_encoder encoder(AV_CODEC_ID_MJPEG, 0 /* MJPEG is constant-quality; bitrate unused */, p.w, p.h, {1,1}, AV_PIX_FMT_YUVJ420P, 0, 1, 0, 0);
    encoder.attach_buffer(p.yuv->data(), p.yuv->size(), 0);
    if(encoder.encode() != R_CODEC_STATE_HAS_OUTPUT)
        return false;
    auto pi = encoder.get();
    vector<uint8_t> out(pi.size);
    memcpy(out.data(), pi.data, pi.size);

    {
        lock_guard<mutex> g(_picture_lock);
        _jpeg_ts = p.ts;
        _jpeg = out;
    }
    jpeg = std::move(out);
    ts = p.ts;
    return true;
}

size_t r_frame_tap::frames_decoded() const { return _frames_decoded.load(); }
size_t r_frame_tap::pictures_made() const { return _pictures_made.load(); }
size_t r_frame_tap::queue_resets() const { return _queue_resets.load(); }

// ---------------------------------------------------------------- r_frame_taps

r_frame_taps::r_frame_taps(r_frame_tap::extradata_fn extradata, clock_fn now) :
    _extradata(std::move(extradata)),
    _now(now ? std::move(now) : clock_fn([]() { return steady_clock::now(); }))
{
}

vector<shared_ptr<r_frame_tap>> r_frame_taps::_take_idle(steady_clock::time_point now)
{
    vector<shared_ptr<r_frame_tap>> idle;
    for(auto it = _taps.begin(); it != _taps.end();)
    {
        if(now - it->second.last_asked >= milliseconds(IDLE_MS))
        {
            idle.push_back(std::move(it->second.tap));
            it = _taps.erase(it);
        }
        else
            ++it;
    }
    _count.store(_taps.size());
    return idle;
}

void r_frame_taps::post(const string& camera_id, const uint8_t* data, size_t size, bool key, int64_t ts, const string& codec_name, const string& codec_params)
{
    if(_count.load() == 0)
        return;

    // No reaping here: destroying an idle tap joins its decoder thread, and this
    // runs on a camera's recording thread, which mustn't wait on that. newest()
    // and the keeper's loop reap.
    shared_ptr<r_frame_tap> tap;
    {
        lock_guard<mutex> g(_lock);
        auto found = _taps.find(camera_id);
        if(found != _taps.end())
            tap = found->second.tap;
    }

    if(tap)
        tap->post(data, size, key, ts, codec_name, codec_params);
}

r_frame_tap_result r_frame_taps::newest(const string& camera_id, uint16_t long_edge, int64_t after)
{
    r_frame_tap_result result;
    shared_ptr<r_frame_tap> tap;
    vector<shared_ptr<r_frame_tap>> idle;
    {
        lock_guard<mutex> g(_lock);
        auto now = _now();
        idle = _take_idle(now);
        auto found = _taps.find(camera_id);
        if(found == _taps.end())
        {
            if(_taps.size() >= MAX_TAPS)
            {
                result.status = r_frame_tap_status::too_many;
                return result;
            }
            found = _taps.emplace(camera_id, _entry{make_shared<r_frame_tap>(long_edge, _extradata), now}).first;
            _count.store(_taps.size());
        }
        found->second.last_asked = now;
        tap = found->second.tap;
    }
    idle.clear();

    if(tap->newest_jpeg(after, long_edge, result.jpeg, result.ts))
        result.status = r_frame_tap_status::picture;
    return result;
}

void r_frame_taps::reap()
{
    vector<shared_ptr<r_frame_tap>> idle;
    {
        lock_guard<mutex> g(_lock);
        idle = _take_idle(_now());
    }
}

size_t r_frame_taps::active() const
{
    return _count.load();
}
