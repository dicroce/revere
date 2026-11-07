
#include "r_av/r_hw_accel.h"
#include "r_utils/r_logger.h"

#include <cstdarg>
#include <map>
#include <mutex>

using namespace r_av;

namespace
{
    // Both probe functions below answer a question about the MACHINE — which
    // hardware backends exist and work — and they answer it by actually
    // creating and then destroying a hardware device context per candidate.
    // That is expensive, and repeating it is genuinely dangerous.
    //
    // A crash dump (2026-08-03) caught it: every /jpg and /webp request routes
    // through _decode_single_frame, which called r_find_best_hw_accel() before
    // constructing its decoder. With the cloud plugin's ~22 snapshot workers
    // driving those endpoints, revere was creating and tearing down D3D11
    // devices continuously from many threads at once. Intel's user-mode driver
    // eventually fail-fasted inside the device destructor:
    //
    //   r_ws::_get_jpg -> query_get_jpg -> _decode_single_frame
    //     -> r_find_best_hw_accel -> av_buffer_unref
    //     -> d3d11!NDXGI::CDevice::~CDevice -> igd10umt64xe!... -> int 29h
    //
    // The answer cannot change while the process runs, so probe once per codec
    // and remember it. The lock is deliberately held ACROSS the probe, not just
    // around the cache: that way at most one hardware device probe is ever in
    // flight, instead of one per concurrent request.
    std::mutex g_probe_lok;
    std::map<AVCodecID, r_hw_accel> g_decoder_cache;
    std::map<AVCodecID, r_hw_accel> g_encoder_cache;

    // FFmpeg says why a device couldn't be created only in its log, which
    // otherwise goes to stderr (nowhere, on Windows). While r_hw_device_create
    // runs, this thread's error and warning lines are also kept for the caller.
    // Other threads, and other times, log as before.
    thread_local std::string* t_device_log = nullptr;
    std::once_flag g_log_callback_once;
    int (*g_device_create)(AVBufferRef**, AVHWDeviceType) = nullptr;
    int (*g_encoder_open)(AVCodecContext*, const AVCodec*, AVDictionary**) = nullptr;

    void _log_callback(void* avcl, int level, const char* fmt, va_list vl)
    {
        if(t_device_log && level <= AV_LOG_WARNING)
        {
            va_list copy;
            va_copy(copy, vl);
            char line[512];
            int print_prefix = 1;
            av_log_format_line(avcl, level, fmt, copy, line, sizeof(line), &print_prefix);
            va_end(copy);
            t_device_log->append(line);
        }
        av_log_default_callback(avcl, level, fmt, vl);
    }
}

static const char* _accel_name(r_hw_accel accel)
{
    switch(accel)
    {
        case r_hw_accel::cuda:         return "cuda";
        case r_hw_accel::qsv:          return "qsv";
        case r_hw_accel::d3d11va:      return "d3d11va";
        case r_hw_accel::vaapi:        return "vaapi";
        case r_hw_accel::videotoolbox: return "videotoolbox";
        default:                       return "software";
    }
}

AVHWDeviceType r_av::r_hw_accel_to_device_type(r_hw_accel accel)
{
    switch(accel)
    {
        case r_hw_accel::cuda:         return AV_HWDEVICE_TYPE_CUDA;
        case r_hw_accel::qsv:          return AV_HWDEVICE_TYPE_QSV;
        case r_hw_accel::d3d11va:      return AV_HWDEVICE_TYPE_D3D11VA;
        case r_hw_accel::vaapi:        return AV_HWDEVICE_TYPE_VAAPI;
        case r_hw_accel::videotoolbox: return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
        default:                       return AV_HWDEVICE_TYPE_NONE;
    }
}

namespace
{
    // Runs `call` with this thread's FFmpeg error and warning lines kept in `why`, as one line.
    template<typename F>
    int _with_logged_why(std::string& why, F call)
    {
        std::call_once(g_log_callback_once, [] { av_log_set_callback(_log_callback); });
        std::string logged;
        t_device_log = &logged;
        int ret = call();
        t_device_log = nullptr;
        // One line: FFmpeg's pieces end in newlines.
        why.clear();
        for(char c : logged)
        {
            if(c == '\n' || c == '\r')
            {
                if(!why.empty() && why.back() != ' ')
                    why += ' ';
            }
            else why += c;
        }
        while(!why.empty() && why.back() == ' ')
            why.pop_back();
        return ret;
    }
}

int r_av::r_hw_device_create(AVBufferRef** device_ctx, AVHWDeviceType device_type, std::string& why)
{
    return _with_logged_why(why, [&] {
        return g_device_create ? g_device_create(device_ctx, device_type)
                               : av_hwdevice_ctx_create(device_ctx, device_type, nullptr, nullptr, 0);
    });
}

int r_av::r_hw_encoder_open(AVCodecContext* context, const AVCodec* codec, AVDictionary** opts, std::string& why)
{
    return _with_logged_why(why, [&] {
        return g_encoder_open ? g_encoder_open(context, codec, opts) : avcodec_open2(context, codec, opts);
    });
}

void r_av::r_hw_encoder_open_override(int (*open)(AVCodecContext*, const AVCodec*, AVDictionary**))
{
    g_encoder_open = open;
}

void r_av::r_hw_device_create_override(int (*create)(AVBufferRef**, AVHWDeviceType))
{
    g_device_create = create;
}

AVPixelFormat r_av::r_hw_accel_get_pix_fmt(const AVCodec* codec, AVHWDeviceType device_type)
{
    for(int i = 0; ; ++i)
    {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
        if(!config)
            break;
        if((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
           config->device_type == device_type)
            return config->pix_fmt;
    }
    return AV_PIX_FMT_NONE;
}

const char* r_av::r_hw_accel_encoder_name(r_hw_accel accel, AVCodecID codec_id)
{
    bool h265 = (codec_id == AV_CODEC_ID_HEVC);
    switch(accel)
    {
        case r_hw_accel::cuda:         return h265 ? "hevc_nvenc"        : "h264_nvenc";
        case r_hw_accel::qsv:          return h265 ? "hevc_qsv"          : "h264_qsv";
        case r_hw_accel::vaapi:        return h265 ? "hevc_vaapi"        : "h264_vaapi";
        case r_hw_accel::videotoolbox: return h265 ? "hevc_videotoolbox" : "h264_videotoolbox";
        default:                       return nullptr;  // d3d11va is decode-only
    }
}

AVPixelFormat r_av::r_hw_accel_encoder_pix_fmt(r_hw_accel accel)
{
    switch(accel)
    {
        case r_hw_accel::qsv:
        case r_hw_accel::vaapi:  return AV_PIX_FMT_NV12;
        default:                 return AV_PIX_FMT_YUV420P;
    }
}

r_hw_accel r_av::r_find_best_hw_accel_encoder(AVCodecID codec_id)
{
    // See the note on g_probe_lok: probe at most once per codec, and never
    // concurrently.
    std::lock_guard<std::mutex> probe_guard(g_probe_lok);

    auto cached = g_encoder_cache.find(codec_id);
    if(cached != g_encoder_cache.end())
        return cached->second;

#if defined(IS_WINDOWS)
    static const r_hw_accel candidates[] = {
        r_hw_accel::cuda,
        r_hw_accel::qsv,
        r_hw_accel::none
    };
#elif defined(IS_MACOS)
    static const r_hw_accel candidates[] = {
        r_hw_accel::videotoolbox,
        r_hw_accel::none
    };
#else  // Linux
    static const r_hw_accel candidates[] = {
        r_hw_accel::cuda,
        r_hw_accel::vaapi,
        r_hw_accel::qsv,
        r_hw_accel::none
    };
#endif

    for(auto accel : candidates)
    {
        if(accel == r_hw_accel::none)
            break;

        const char* enc_name = r_hw_accel_encoder_name(accel, codec_id);
        if(!enc_name)
            continue;

        if(!avcodec_find_encoder_by_name(enc_name))
        {
            continue;
        }

        auto device_type = r_hw_accel_to_device_type(accel);
        if(device_type != AV_HWDEVICE_TYPE_NONE)
        {
            AVBufferRef* hw_device_ctx = nullptr;
            int ret = av_hwdevice_ctx_create(&hw_device_ctx, device_type, nullptr, nullptr, 0);
            if(ret < 0)
            {
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                continue;
            }
            av_buffer_unref(&hw_device_ctx);
        }

        g_encoder_cache[codec_id] = accel;
        R_LOG_INFO("r_av: hardware encoder for codec %d -> %s (probed once, cached)",
                   (int)codec_id, _accel_name(accel));
        return accel;
    }

    g_encoder_cache[codec_id] = r_hw_accel::none;
    return r_hw_accel::none;
}

r_hw_accel r_av::r_find_best_hw_accel(AVCodecID codec_id)
{
    // See the note on g_probe_lok: probe at most once per codec, and never
    // concurrently. This is the path that was being hit per /jpg request.
    std::lock_guard<std::mutex> probe_guard(g_probe_lok);

    auto cached = g_decoder_cache.find(codec_id);
    if(cached != g_decoder_cache.end())
        return cached->second;

    const AVCodec* codec = avcodec_find_decoder(codec_id);
    if(!codec)
    {
        g_decoder_cache[codec_id] = r_hw_accel::none;
        return r_hw_accel::none;
    }

#if defined(IS_WINDOWS)
    static const r_hw_accel candidates[] = {
        r_hw_accel::cuda,
        r_hw_accel::qsv,
        r_hw_accel::d3d11va,
        r_hw_accel::none
    };
#elif defined(IS_MACOS)
    static const r_hw_accel candidates[] = {
        r_hw_accel::videotoolbox,
        r_hw_accel::none
    };
#else  // Linux
    static const r_hw_accel candidates[] = {
        r_hw_accel::cuda,
        r_hw_accel::vaapi,
        r_hw_accel::qsv,
        r_hw_accel::none
    };
#endif

    for(auto accel : candidates)
    {
        if(accel == r_hw_accel::none)
            break;

        auto device_type = r_hw_accel_to_device_type(accel);

        if(r_hw_accel_get_pix_fmt(codec, device_type) == AV_PIX_FMT_NONE)
        {
            continue;
        }

        AVBufferRef* hw_device_ctx = nullptr;
        std::string why;
        int ret = r_hw_device_create(&hw_device_ctx, device_type, why);
        if(ret < 0)
        {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            R_LOG_INFO("r_av: %s decode for codec %d unavailable: %s%s%s", _accel_name(accel), (int)codec_id,
                       errbuf, why.empty() ? "" : ": ", why.c_str());
            continue;
        }

        av_buffer_unref(&hw_device_ctx);

        g_decoder_cache[codec_id] = accel;
        R_LOG_INFO("r_av: hardware decoder for codec %d -> %s (probed once, cached)",
                   (int)codec_id, _accel_name(accel));
        return accel;
    }

    g_decoder_cache[codec_id] = r_hw_accel::none;
    return r_hw_accel::none;
}

void r_av::r_hw_accel_decode_failed(AVCodecID codec_id)
{
    std::lock_guard<std::mutex> probe_guard(g_probe_lok);
    auto it = g_decoder_cache.find(codec_id);
    if(it != g_decoder_cache.end() && it->second == r_hw_accel::none)
        return;
    R_LOG_WARNING("r_av: %s decode for codec %d failed on this machine's driver; decoding in software from now on",
                  it != g_decoder_cache.end() ? _accel_name(it->second) : "hardware", (int)codec_id);
    g_decoder_cache[codec_id] = r_hw_accel::none;
}
