#ifndef __r_vss_r_live_restream_host_h
#define __r_vss_r_live_restream_host_h

#include "r_disco/r_camera.h"
#include "r_pipeline/r_stream_info.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <functional>
#include <map>
#include <memory>
#include <string>

struct _GstRTSPMedia;
struct _GstRTSPMountPoints;

namespace r_vss
{

struct live_restreaming_state;
class r_substream_recorder;

// What a live restream needs from whatever serves its mounts: the stream keeper, or a bare RTSP server in a test.
// It keeps each connected viewer's state (found by the key its stream posts under), and mounts and unmounts the
// substream's path.
class r_live_restream_host
{
public:
    virtual ~r_live_restream_host() = default;

    virtual void add_live_restreaming_state(_GstRTSPMedia* media, std::shared_ptr<live_restreaming_state> lrs) = 0;
    virtual void remove_live_restreaming_state(_GstRTSPMedia* media) = 0;
    virtual void iterate_live_restreaming_states(const std::string& key, std::function<void(live_restreaming_state&)> fn) = 0;

    // The camera's substream mount (substream_mount_path), served by `recorder`. Throws when the SDP has no video.
    virtual std::string add_substream_mount(const r_disco::r_camera& camera, const std::map<std::string, r_pipeline::r_sdp_media>& sdp_medias, r_utils::r_nullable<r_pipeline::r_encoding> video_encoding, r_substream_recorder* recorder) = 0;
    virtual void remove_restream_mount(const std::string& path) = 0;
};

// The substream mount on `mounts` for `recorder` (video only, viewers need the viewer role): what a host's
// add_substream_mount does. Returns the path.
R_API std::string add_substream_mount_to(_GstRTSPMountPoints* mounts, const r_disco::r_camera& camera, const std::map<std::string, r_pipeline::r_sdp_media>& sdp_medias, r_utils::r_nullable<r_pipeline::r_encoding> video_encoding, r_substream_recorder* recorder);

}

#endif
