
#ifndef r_av_r_hw_accel_h
#define r_av_r_hw_accel_h

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

#include "r_utils/r_macro.h"

#include <string>

namespace r_av
{

enum class r_hw_accel
{
    none,
    cuda,         // NVIDIA (Windows & Linux)
    qsv,          // Intel Quick Sync (Windows & Linux)
    d3d11va,      // Windows D3D11VA
    vaapi,        // Linux / AMD
    videotoolbox  // macOS
};

// Probes available hardware in platform-preferred order and returns the best
// backend that can decode codec_id, or r_hw_accel::none for software fallback.
R_API r_hw_accel r_find_best_hw_accel(AVCodecID codec_id);

// A decoder found that r_find_best_hw_accel's choice can't decode codec_id after
// all: the device opened, but the driver refused the stream (e.g. a VAAPI driver
// without H.264). From now on r_find_best_hw_accel returns none for codec_id, so
// later decoders start in software instead of failing the same way.
R_API void r_hw_accel_decode_failed(AVCodecID codec_id);

// Probes available hardware encoders and returns the best backend, or none.
// Note: D3D11VA is decode-only and is never returned by this function.
R_API r_hw_accel r_find_best_hw_accel_encoder(AVCodecID codec_id);

// Utilities used internally by decoder/encoder — not part of the public API
// but exposed here so both can share them without duplication.
AVHWDeviceType r_hw_accel_to_device_type(r_hw_accel accel);
AVPixelFormat  r_hw_accel_get_pix_fmt(const AVCodec* codec, AVHWDeviceType device_type);

// Creates a hardware device context (av_hwdevice_ctx_create) and returns its
// result. On failure `why` gets the lines FFmpeg logged during the attempt:
// for CUDA, the call that failed and its CUDA error, which the return code
// alone (AVERROR_EXTERNAL, "Generic error in an external library") doesn't say.
R_API int r_hw_device_create(AVBufferRef** device_ctx, AVHWDeviceType device_type, std::string& why);

// Tests only: r_hw_device_create calls `create` instead of
// av_hwdevice_ctx_create (nullptr restores it).
R_API void r_hw_device_create_override(int (*create)(AVBufferRef**, AVHWDeviceType));

// Opens an encoder (avcodec_open2) and returns its result. On failure `why` gets
// the lines FFmpeg logged during the attempt, as for r_hw_device_create: a hardware
// encoder says why it refused (no session left, driver, memory) only there, the
// return code is AVERROR_EXTERNAL.
R_API int r_hw_encoder_open(AVCodecContext* context, const AVCodec* codec, AVDictionary** opts, std::string& why);

// Tests only: r_hw_encoder_open calls `open` instead of avcodec_open2
// (nullptr restores it).
R_API void r_hw_encoder_open_override(int (*open)(AVCodecContext*, const AVCodec*, AVDictionary**));

// Returns the FFmpeg encoder codec name for a given hw backend and codec id
// (e.g. qsv + H264 → "h264_qsv"). Returns nullptr for unsupported combinations
// (d3d11va, none, or unknown codec_id).
const char* r_hw_accel_encoder_name(r_hw_accel accel, AVCodecID codec_id);

// Returns the pixel format the hw encoder expects as input. Used by
// r_video_encoder to decide whether a YUV420P→NV12 conversion is needed.
AVPixelFormat  r_hw_accel_encoder_pix_fmt(r_hw_accel accel);

}

#endif
