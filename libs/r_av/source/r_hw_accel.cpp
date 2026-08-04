
#include "r_av/r_hw_accel.h"
#include "r_utils/r_logger.h"

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
        int ret = av_hwdevice_ctx_create(&hw_device_ctx, device_type, nullptr, nullptr, 0);
        if(ret < 0)
        {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
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
