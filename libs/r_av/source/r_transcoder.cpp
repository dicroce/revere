
#include "r_av/r_transcoder.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"
#include <algorithm>

using namespace r_av;
using namespace r_utils;
using namespace std;

r_transcoder::r_transcoder(
    const std::string& output_url,
    const std::string& output_format,
    AVCodecID input_codec,
    const std::vector<uint8_t>& input_extradata,
    AVRational input_timebase,
    uint16_t output_width,
    uint16_t output_height,
    AVRational framerate,
    uint32_t initial_bitrate,
    uint32_t max_bitrate,
    uint32_t min_bitrate,
    bool enable_dynamic_bitrate
) :
    _running(false),
    _max_queue_size(30),
    _decoder(input_codec),
    _encoder(),
    _muxer(output_url, false, output_format),
    _current_bitrate(initial_bitrate),
    _max_bitrate(max_bitrate),
    _min_bitrate(min_bitrate),
    _enable_dynamic_bitrate(enable_dynamic_bitrate),
    _bytes_encoded_this_window(0),
    _output_width(output_width),
    _output_height(output_height),
    _framerate(framerate),
    _profile(AV_PROFILE_H264_MAIN),
    _level(41),
    _frame_count(0),
    _input_timebase(input_timebase),
    _next_pts(-1),
    _last_decoded_frame(nullptr)
{
    // Set extradata on decoder
    if (!input_extradata.empty())
    {
        _decoder.set_extradata(input_extradata);
    }

    // Create encoder
    _encoder = r_video_encoder(
        AV_CODEC_ID_H264,
        initial_bitrate,
        output_width,
        output_height,
        framerate,
        AV_PIX_FMT_YUV420P,
        0,
        (uint16_t)framerate.num,
        _profile,
        _level,
        "veryfast",
        "zerolatency"
    );

    // Setup muxer
    _muxer.add_video_stream(framerate, AV_CODEC_ID_H264, output_width, output_height, _profile, _level);
    _muxer.set_video_extradata(_encoder.get_extradata());
    _muxer.open();
}

r_transcoder::~r_transcoder()
{
    stop();
}

void r_transcoder::start()
{
    if (_running.exchange(true))
        return; // Already running

    _last_bitrate_check = std::chrono::steady_clock::now();
    _bytes_encoded_this_window = 0;
    _frame_count = 0;
    _next_pts = -1;
    _last_decoded_frame = nullptr;

    _worker = std::thread(&r_transcoder::_worker_thread, this);
}

void r_transcoder::stop()
{
    if (!_running.exchange(false))
        return; // Not running

    _queue_cv.notify_all();

    if (_worker.joinable())
        _worker.join();
}

void r_transcoder::write_frame(const uint8_t* data, size_t size, int64_t pts)
{
    std::unique_lock<std::mutex> lock(_queue_mutex);

    // Block if queue is full (backpressure)
    _queue_cv.wait(lock, [this] {
        return _input_queue.size() < _max_queue_size || !_running;
    });

    if (!_running)
        return;

    queued_frame frame;
    frame.data.resize(size);
    memcpy(frame.data.data(), data, size);
    frame.pts = pts;

    _input_queue.push_back(std::move(frame));
    _queue_cv.notify_one();
}

void r_transcoder::_worker_thread()
{
    try
    {
        AVRational encoder_tb;
        encoder_tb.num = _framerate.den;
        encoder_tb.den = _framerate.num;

        while (_running)
        {
            queued_frame frame;

            // 1. Get next frame from input queue
            {
                std::unique_lock<std::mutex> lock(_queue_mutex);

                // Wait for data or shutdown
                _queue_cv.wait(lock, [this] {
                    return !_input_queue.empty() || !_running;
                });

                if (!_running && _input_queue.empty())
                    break;

                if (_input_queue.empty())
                    continue;

                frame = std::move(_input_queue.front());
                _input_queue.pop_front();
            }

            // Notify write_frame() that space is available
            _queue_cv.notify_one();

            // 2. Decode
            _decoder.attach_buffer(frame.data.data(), frame.data.size());
            auto decode_state = _decoder.decode();

            if (decode_state != R_CODEC_STATE_HAS_OUTPUT &&
                decode_state != R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                continue;

            // 3. Get decoded frame (with scaling)
            auto decoded = _decoder.get(
                AV_PIX_FMT_YUV420P,
                _output_width,
                _output_height,
                1
            );

            // 4. Resampling Logic
            // Convert input PTS to encoder timebase
            int64_t rescaled_pts = av_rescale_q(frame.pts, _input_timebase, encoder_tb);

            if (_next_pts == -1)
                _next_pts = rescaled_pts;

            // If input is behind expected, drop it (except if it's the very first one, but we handled that)
            if (rescaled_pts < _next_pts)
            {
                // Drop frame to catch up
                continue;
            }

            // If input is ahead, duplicate previous frame to fill gap
            while (rescaled_pts > _next_pts)
            {
                if (_last_decoded_frame)
                {
                    _encoder.attach_buffer(_last_decoded_frame->data(), _last_decoded_frame->size(), _next_pts);
                    while(true)
                    {
                        auto encode_state = _encoder.encode();
                        if (encode_state == R_CODEC_STATE_HAS_OUTPUT)
                        {
                            auto pi = _encoder.get();
                            _muxer.write_video_frame(pi.data, pi.size, pi.pts, pi.dts, pi.time_base, pi.key);
                            _bytes_encoded_this_window += pi.size;
                        }
                        else break;
                    }
                }
                _next_pts++;
            }

            // Encode current frame
            _encoder.attach_buffer(decoded->data(), decoded->size(), _next_pts);
            
            while(true)
            {
                auto encode_state = _encoder.encode();

                if (encode_state == R_CODEC_STATE_HAS_OUTPUT)
                {
                    auto pi = _encoder.get();
                    _muxer.write_video_frame(pi.data, pi.size, pi.pts, pi.dts, pi.time_base, pi.key);
                    _bytes_encoded_this_window += pi.size;
                }
                else break;
            }

                // Check bitrate adjustment
                if (_enable_dynamic_bitrate)
                {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        now - _last_bitrate_check
                    ).count();

                    if (elapsed >= 5)
                    {
                        _adjust_bitrate();
                        _last_bitrate_check = now;
                        _bytes_encoded_this_window = 0;
                    }
                }

            _last_decoded_frame = decoded;
            _next_pts++;
        }

        // Flush encoder
        while (_encoder.flush() == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto pi = _encoder.get();
            _muxer.write_video_frame(pi.data, pi.size, pi.pts, pi.dts, pi.time_base, pi.key);
        }

        _muxer.finalize();
    }
    catch (const std::exception& ex)
    {
        R_LOG_ERROR("Transcoder worker thread error: %s", ex.what());
    }
}

void r_transcoder::_adjust_bitrate()
{
    // Calculate actual bitrate over the last window
    uint32_t actual_bitrate = static_cast<uint32_t>((_bytes_encoded_this_window * 8) / 5); // bits per second

    // Calculate how far we are from target
    float ratio = (float)actual_bitrate / (float)_current_bitrate;

    uint32_t new_bitrate = _current_bitrate;

    // If we're consistently under target, we can increase
    if (ratio < 0.85f && _current_bitrate < _max_bitrate)
    {
        // Increase by 25%
        new_bitrate = (std::min)(_max_bitrate,
                                 (uint32_t)(_current_bitrate * 1.25f));
    }
    // If we're over target, we need to decrease
    else if (ratio > 1.15f)
    {
        // Decrease by 30%
        new_bitrate = (std::max)(_min_bitrate,
                                 (uint32_t)(_current_bitrate * 0.70f));
    }

    // Only adjust if bitrate changed significantly (>10%)
    if (std::abs((int)new_bitrate - (int)_current_bitrate) > (int)(_current_bitrate * 0.10f))
    {
        R_LOG_INFO("Adjusting bitrate: %u -> %u (actual: %u)", _current_bitrate, new_bitrate, actual_bitrate);

        // Update encoder bitrate dynamically
        _encoder.set_bitrate(new_bitrate);
        _current_bitrate = new_bitrate;
    }
}
