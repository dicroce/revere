#ifndef r_av_r_ffmpeg_compat_h
#define r_av_r_ffmpeg_compat_h

extern "C"
{
#include <libavcodec/avcodec.h>
}

// FFmpeg 6.1 renamed FF_PROFILE_* to AV_PROFILE_*; older ones (Debian
// bookworm and Raspberry Pi OS ship 5.1) only have the FF_ names. Included by
// the r_av headers whose APIs take a profile (r_muxer, r_video_encoder), so
// their users, plugins included, can use the new names.
#ifndef AV_PROFILE_H264_BASELINE
#define AV_PROFILE_H264_BASELINE FF_PROFILE_H264_BASELINE
#endif
#ifndef AV_PROFILE_H264_MAIN
#define AV_PROFILE_H264_MAIN FF_PROFILE_H264_MAIN
#endif
#ifndef AV_PROFILE_H264_HIGH
#define AV_PROFILE_H264_HIGH FF_PROFILE_H264_HIGH
#endif
#ifndef AV_PROFILE_HEVC_MAIN
#define AV_PROFILE_HEVC_MAIN FF_PROFILE_HEVC_MAIN
#endif

#endif
