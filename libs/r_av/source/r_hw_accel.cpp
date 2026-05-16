
#include "r_av/r_hw_accel.h"
#include "r_utils/r_logger.h"

using namespace r_av;

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
    R_LOG_INFO("r_find_best_hw_accel_encoder: probing for codec_id=%d", (int)codec_id);

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
            R_LOG_INFO("r_find_best_hw_accel_encoder: skipping %s — encoder '%s' not in FFmpeg build", _accel_name(accel), enc_name);
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
                R_LOG_INFO("r_find_best_hw_accel_encoder: skipping %s — device creation failed: %s", _accel_name(accel), errbuf);
                continue;
            }
            av_buffer_unref(&hw_device_ctx);
        }

        R_LOG_INFO("r_find_best_hw_accel_encoder: selected %s (%s)", _accel_name(accel), enc_name);
        return accel;
    }

    R_LOG_INFO("r_find_best_hw_accel_encoder: all candidates exhausted, falling back to software");
    return r_hw_accel::none;
}

r_hw_accel r_av::r_find_best_hw_accel(AVCodecID codec_id)
{
    R_LOG_INFO("r_find_best_hw_accel: probing for codec_id=%d", (int)codec_id);

    const AVCodec* codec = avcodec_find_decoder(codec_id);
    if(!codec)
    {
        R_LOG_INFO("r_find_best_hw_accel: avcodec_find_decoder returned null, falling back to software");
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
            R_LOG_INFO("r_find_best_hw_accel: skipping %s — codec has no hw config for this device type", _accel_name(accel));
            continue;
        }

        AVBufferRef* hw_device_ctx = nullptr;
        int ret = av_hwdevice_ctx_create(&hw_device_ctx, device_type, nullptr, nullptr, 0);
        if(ret < 0)
        {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            R_LOG_INFO("r_find_best_hw_accel: skipping %s — device creation failed: %s", _accel_name(accel), errbuf);
            continue;
        }

        av_buffer_unref(&hw_device_ctx);
        return accel;
    }

    R_LOG_INFO("r_find_best_hw_accel: all candidates exhausted, falling back to software");
    return r_hw_accel::none;
}
