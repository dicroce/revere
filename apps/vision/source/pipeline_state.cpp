
#include "r_pipeline/r_stream_info.h"
#include "r_pipeline/r_sample_context.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_blob_tree.h"
#include "pipeline_state.h"
#include <algorithm>
#include <cstdint>
#include "pipeline_host.h"
#include "query.h"

using namespace vision;
using namespace r_pipeline;
using namespace r_utils;
using namespace r_http;
using namespace std;
using namespace std::chrono;

static void aspect_correct_video_dimensions(
    uint16_t streamWidth,
    uint16_t streamHeight,
    uint16_t requestedWidth,
    uint16_t requestedHeight,
    uint16_t& destWidth,
    uint16_t& destHeight
)
{
    destWidth = requestedWidth;
    destHeight = requestedHeight;

    // encode size
    if(streamWidth != 0 && streamHeight !=0)
    {
        uint16_t newEncodeWidth;
        uint16_t newEncodeHeight;

        if(requestedHeight != 0 && requestedWidth != 0)
        {
            float streamAspectRatio = streamWidth * 1.0f / streamHeight;
            float maxAspectRatio = requestedWidth * 1.0f / requestedHeight;
            float scaleFactor;

            if(maxAspectRatio < streamAspectRatio)
                scaleFactor = requestedWidth * 1.0f / streamWidth;
            else
                scaleFactor = requestedHeight * 1.0f / streamHeight;

            uint16_t scaledRoundedPixelWidth = (uint16_t)(streamWidth * scaleFactor + 0.5);
            uint16_t scaledRoundedPixelHeight = (uint16_t)(streamHeight * scaleFactor + 0.5);

            uint16_t multipleOfEightWidth = (uint16_t)(max( scaledRoundedPixelWidth / 8, 1) * 8);
            uint16_t multipleOfEightHeight = (uint16_t)(max( scaledRoundedPixelHeight / 8, 1) * 8);

            newEncodeWidth = multipleOfEightWidth;
            newEncodeHeight = multipleOfEightHeight;
        }
        else
        {
            newEncodeWidth = streamWidth;
            newEncodeHeight = streamHeight;
        }

        if(requestedWidth != newEncodeWidth)
            destWidth = newEncodeWidth;

        if(requestedHeight != newEncodeHeight)
            destHeight = newEncodeHeight;
    }
}

pipeline_state::pipeline_state(const stream_info& si, pipeline_host* ph, uint16_t w, uint16_t h, configure_state& cfg_state) :
    _si(si),
    _ph(ph),
    _w(w),
    _h(h),
    _source(),
    _running(false),
    _process_th(),
    _process_q(),
    _last_video_sample(),
    _video_decoder(),
    _audio_decoder(),
    _sdl_audio_device(0),
    _volume_gain(1.0f),
    _audio_active(false),
    _has_audio(false),
    _received_first_frame(false),
    _last_v_pts(0),
    _last_a_pts(0),
    _v_pts_at_check(0),
    _a_pts_at_check(0),
    _cfg_state(cfg_state),
    _last_control_bar_pos(),
    _range_start(),
    _range_end()
{
    // The callbacks for audio and video sample post the arriving buffers (as samples) onto the
    // _process_q. The process thread (in the pipeline_state) then pulls samples from the process_q
    // and decodes them.

    _source.set_audio_sample_cb([this](const sample_context& sc, const r_gst_buffer& buffer, bool key, int64_t pts){
        this->_has_audio = true;
        this->_last_a_pts = pts;

        if(_audio_decoder.is_null())
        {
            auto maybe_encoding = sc.audio_encoding();
            if(!maybe_encoding.is_null())
            {
                auto enc = maybe_encoding.value();
                auto sr = sc.audio_sample_rate();
                auto ch = sc.audio_channels();
                int sample_rate = sr.is_null() ? 8000 : (int)sr.value();
                int channels   = ch.is_null() ? 1    : (int)ch.value();

                if(enc == PCMU_ENCODING)
                {
                    _audio_decoder.assign(r_av::r_audio_decoder(AV_CODEC_ID_PCM_MULAW));
                    _audio_decoder.raw().set_pcm_params(sample_rate, channels);
                }
                else if(enc == PCMA_ENCODING)
                {
                    _audio_decoder.assign(r_av::r_audio_decoder(AV_CODEC_ID_PCM_ALAW));
                    _audio_decoder.raw().set_pcm_params(sample_rate, channels);
                }
                else if(enc == AAC_LATM_ENCODING || enc == AAC_GENERIC_ENCODING)
                {
                    auto codec_id = (enc == AAC_LATM_ENCODING) ? AV_CODEC_ID_AAC_LATM : AV_CODEC_ID_AAC;
                    _audio_decoder.assign(r_av::r_audio_decoder(codec_id));
                    auto media = sc.sdp_media(AUDIO_MEDIA);
                    string audio_codec_name, audio_codec_params;
                    int audio_timebase;
                    tie(audio_codec_name, audio_codec_params, audio_timebase) = sdp_media_to_s(media);
                    auto asc = get_audio_codec_extradata(audio_codec_params);
                    if(!asc.empty())
                        _audio_decoder.raw().set_extradata(asc);
                }

                if(!_audio_decoder.is_null() && _sdl_audio_device == 0)
                {
                    SDL_AudioSpec want, have;
                    SDL_zero(want);
                    want.freq     = 44100;
                    want.format   = AUDIO_S16SYS;
                    want.channels = 2;
                    want.samples  = 4096;
                    want.callback = nullptr;
                    _sdl_audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
                    if(_sdl_audio_device != 0)
                    {
                        R_LOG_INFO("Audio device opened: freq=%d channels=%d", have.freq, have.channels);
                        SDL_PauseAudioDevice(_sdl_audio_device, _audio_active.load() ? 0 : 1);
                    }
                    else R_LOG_ERROR("SDL_OpenAudioDevice failed: %s", SDL_GetError());
                }
            }
        }

        // Decode and queue to SDL directly in this callback — never post audio to _process_q.
        // Posting audio to the video process queue starves video decode and can trigger the
        // dead-stream restart (which switches back to live).
        if(!_audio_decoder.is_null() && _sdl_audio_device != 0)
        {
            // Cap the SDL queue to ~200ms to prevent buffer buildup during fast playback delivery.
            static const Uint32 MAX_QUEUED = 44100 * 2 * sizeof(int16_t) / 5; // 200ms S16 stereo
            if(SDL_GetQueuedAudioSize(_sdl_audio_device) < MAX_QUEUED)
            {
                auto m = buffer.map(r_gst_buffer::MT_READ);
                _audio_decoder.raw().attach_buffer(m.data(), m.size());
                if(_audio_decoder.raw().decode() == r_av::R_CODEC_STATE_HAS_OUTPUT)
                {
                    auto pcm = _audio_decoder.raw().get(AV_SAMPLE_FMT_S16, 44100, 2);
                    if(pcm && !pcm->empty())
                    {
                        float gain = _volume_gain.load();
                        if(gain != 1.0f)
                        {
                            int16_t* samples = reinterpret_cast<int16_t*>(pcm->data());
                            size_t n = pcm->size() / sizeof(int16_t);
                            for(size_t i = 0; i < n; ++i)
                            {
                                int32_t v = (int32_t)((float)samples[i] * gain);
                                samples[i] = (int16_t)std::clamp(v, (int32_t)INT16_MIN, (int32_t)INT16_MAX);
                            }
                        }
                        SDL_QueueAudio(_sdl_audio_device, pcm->data(), (Uint32)pcm->size());
                    }
                }
            }
        }
    });

    _source.set_video_sample_cb([this](const sample_context& sc, const r_gst_buffer& buffer, bool key, int64_t pts){

        if(_video_decoder.is_null())
        {
            auto media = sc.sdp_media(VIDEO_MEDIA);

            auto video_encoding = media.rtpmaps[media.formats.front()].encoding;

            if(video_encoding == H264_ENCODING)
            {
                this->_video_decoder.assign(r_av::r_video_decoder(AV_CODEC_ID_H264, r_av::r_find_best_hw_accel(AV_CODEC_ID_H264)));

                string video_codec_name, h264_codec_parameters;
                int video_timebase;
                tie(video_codec_name, h264_codec_parameters, video_timebase) = sdp_media_to_s(media);

                auto h264_sps_b = get_h264_sps(h264_codec_parameters);
                auto h264_pps_b = get_h264_sps(h264_codec_parameters);

                this->_video_decoder.raw().set_extradata(make_h264_extradata(h264_sps_b, h264_pps_b));
            }
            else if(video_encoding == H265_ENCODING)
            {
                this->_video_decoder.assign(r_av::r_video_decoder(AV_CODEC_ID_H265, r_av::r_find_best_hw_accel(AV_CODEC_ID_H265)));

                string video_codec_name, h265_codec_parameters;
                int video_timebase;
                tie(video_codec_name, h265_codec_parameters, video_timebase) = sdp_media_to_s(media);

                auto h265_vps_b = get_h265_vps(h265_codec_parameters);
                auto h265_sps_b = get_h265_sps(h265_codec_parameters);
                auto h265_pps_b = get_h265_pps(h265_codec_parameters);

                this->_video_decoder.raw().set_extradata(make_h265_extradata(h265_vps_b, h265_sps_b, h265_pps_b));
            }
            else R_THROW(("Unsupported video codec."));
        }

        sample s;
        s.buffer = move(buffer);
        s.media_type = VIDEO_MEDIA;
        // Calculate absolute timestamp: stream start + pts
        this->_last_v_pts = sc.stream_start_ts() + pts;
        this->_received_first_frame = true;
        this->_process_q.post(s);
    });

    _process_th = thread(bind(&pipeline_state::_entry_point, this));
}

pipeline_state::~pipeline_state()
{
    _source.stop();
    _running = false;
    _process_q.wake();
    _process_th.join();
    if(_sdl_audio_device != 0)
    {
        SDL_CloseAudioDevice(_sdl_audio_device);
        _sdl_audio_device = 0;
    }
}

void pipeline_state::resize(uint16_t w, uint16_t h)
{
    _w = w;
    _h = h;

    // Send our last video sample to the pipeline again, to resize it.
    if(!_last_video_sample.is_null())
        _process_q.post(_last_video_sample.value());
}

void pipeline_state::play_live()
{
    _last_play_time = steady_clock::now();

    vector<r_arg> arguments;
    add_argument(arguments, "url", _si.rtsp_url);
    _source.set_args(arguments);

    _source.play();
}

void pipeline_state::play()
{
    // Get the end time of the bar
    // Subtract the current playhead position from the end time to get the duration
    // of the bar.
    // copy the rtsp url to new string
    // Convert to the current playhead position to iso 8601 and append to url
    // add bar duration to playhead position and convert to iso 8601 and append to url.
    // set url on source and call play()

    auto playback_duration = _range_end - _last_control_bar_pos;

    string url = r_string_utils::format(
        "%s_%s_%s",
        _si.rtsp_url.c_str(),
        r_time_utils::tp_to_iso_8601(_last_control_bar_pos, false).c_str(),
        r_time_utils::tp_to_iso_8601(_range_end, false).c_str()
    );
    R_LOG_INFO("  Playback URL: %s", url.c_str());

    _last_play_time = steady_clock::now();

    vector<r_arg> arguments;
    add_argument(arguments, "url", url);
    add_argument(arguments, "protocols", string("TCP"));
    _source.set_args(arguments);

    _source.play();
}

void pipeline_state::stop()
{
    _source.stop();
}

void pipeline_state::control_bar(const system_clock::time_point& pos)
{
    _last_control_bar_pos = pos;

    if(_video_decoder.is_null())
        return;

    auto maybe_revere_ipv4 = _cfg_state.get_revere_ipv4();
    if(maybe_revere_ipv4.is_null())
        return;

    try
    {
        auto time_s = r_time_utils::tp_to_iso_8601(pos, false);

        auto response = query_key(maybe_revere_ipv4.value(), _si.camera_id, time_s);

        if(response.size() > 0)
        {
            uint32_t version = 0;
            auto bt = r_blob_tree::deserialize((uint8_t*)response.data(), response.size(), version);

            if(bt.has_key("frames"))
            {
                if(bt["frames"][0].has_key("data"))
                {
                    auto frame = bt["frames"][0]["data"].get_blob();

                    auto s_ts = bt["frames"][0]["ts"].get_string();

                    r_gst_buffer buffer(frame.data(), frame.size());

                    // Note: 
                    sample s;
                    s.buffer = move(buffer);
                    s.media_type = VIDEO_MEDIA;
                    s.still = true;
                    
                    // result here is a future, and we COULD call .get() on it to block until the frame is processed.
                    auto result = this->_process_q.post(s);
                }
            }
        }    
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }
}

void pipeline_state::_entry_point()
{
    _running = true;
    while(this->_running)
    {
        auto process_q_depth = this->_process_q.size();

        auto maybe_sample = this->_process_q.poll(std::chrono::milliseconds(100));
        if(!maybe_sample.is_null())
        {
            if(maybe_sample.raw().first.media_type == VIDEO_MEDIA)
            {
                _last_video_sample.set_value(maybe_sample.value().first);

                int tries = (maybe_sample.raw().first.still) ? 10 : 1;

                auto m = maybe_sample.raw().first.buffer.map(r_gst_buffer::MT_READ);

                while(tries > 0)
                {
                    _video_decoder.raw().attach_buffer(m.data(), m.size());

                    r_av::r_codec_state decode_state = r_av::R_CODEC_STATE_INITIALIZED;

                    decode_state = _video_decoder.raw().decode();

                    if(decode_state == r_av::R_CODEC_STATE_HAS_OUTPUT)
                    {
                        tries = 0;

                        // If we are behind, drop the frame here

                        if(process_q_depth < 2)
                        {
                            uint16_t input_width = _video_decoder.raw().input_width();
                            uint16_t input_height = _video_decoder.raw().input_height();
                            uint16_t dest_width, dest_height;

                            aspect_correct_video_dimensions(
                                input_width,
                                input_height,
                                _w,
                                _h,
                                dest_width,
                                dest_height
                            );

                            // Use PTS directly - it should already be absolute from the sample callback

                            static int decode_log_count = 0;
                            if (decode_log_count++ < 5)
                            {
                                R_LOG_INFO("Decoding video frame: stream=%s, input=%dx%d, dest=%dx%d, pts=%lld",
                                    _si.name.c_str(), input_width, input_height, dest_width, dest_height, _last_v_pts);
                            }

                            // Use BGRA format which matches SDL_PIXELFORMAT_ARGB8888 on little-endian (x86)
                            // BGRA in memory = B G R A bytes = ARGB8888 pixel format
                            auto decoded_frame = _video_decoder.raw().get(AV_PIX_FMT_BGRA, dest_width, dest_height, 1);

                            static int pixel_log_count = 0;
                            if (decoded_frame && pixel_log_count++ < 5)
                            {
                                const uint8_t* data = decoded_frame->data();
                                R_LOG_INFO("Decoded frame data: size=%zu, first_pixels: B=%d G=%d R=%d A=%d",
                                    decoded_frame->size(), data[0], data[1], data[2], data[3]);
                            }

                            _ph->post_video_frame(
                                _si.name,
                                decoded_frame,
                                dest_width,
                                dest_height,
                                input_width,
                                input_height,
                                _last_v_pts
                            );
                        }
                    }

                    --tries;
                }
            }
            maybe_sample.raw().second.set_value(true);
        }
    }
}