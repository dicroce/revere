
#include "r_disco/r_stream_config.h"
#include "r_utils/r_md5.h"
#include <chrono>

using namespace r_utils;
using namespace r_disco;
using namespace std;

string r_disco::hash_stream_config(const r_stream_config& sc)
{
    r_md5 h;

    h.update((uint8_t*)sc.id.c_str(), sc.id.length());
    if(!sc.ipv4.is_null())
        h.update((uint8_t*)sc.ipv4.value().c_str(), sc.ipv4.value().length());
    if(!sc.xaddrs.is_null())
        h.update((uint8_t*)sc.xaddrs.value().c_str(), sc.xaddrs.value().length());
    if(!sc.address.is_null())
        h.update((uint8_t*)sc.address.value().c_str(), sc.address.value().length());

    if(!sc.rtsp_url.is_null())
        h.update((uint8_t*)sc.rtsp_url.value().c_str(), sc.rtsp_url.value().length());

    if(!sc.video_codec.is_null())
        h.update((uint8_t*)sc.video_codec.value().c_str(), sc.video_codec.value().length());
    if(!sc.video_codec_parameters.is_null())
        h.update((uint8_t*)sc.video_codec_parameters.value().c_str(), sc.video_codec_parameters.value().length());
    if(!sc.video_timebase.is_null())
        h.update((uint8_t*)&sc.video_timebase.value(), sizeof(sc.video_timebase.value()));

    if(!sc.audio_codec.is_null())
        h.update((uint8_t*)sc.audio_codec.value().c_str(), sc.audio_codec.value().length());
    if(!sc.audio_codec_parameters.is_null())
        h.update((uint8_t*)sc.audio_codec_parameters.value().c_str(), sc.audio_codec_parameters.value().length());
    if(!sc.audio_timebase.is_null())
        h.update((uint8_t*)&sc.audio_timebase.value(), sizeof(sc.audio_timebase.value()));

    h.finalize();

    return h.get_as_string();
}
