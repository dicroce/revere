#ifndef __revere_audio_policy_h
#define __revere_audio_policy_h

#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include <map>
#include <set>
#include <string>

namespace r_vss
{

// Whether each camera's audio is recorded, as the cloud says (the organization's, the site's and the camera's own
// audio settings together). When a camera's is off, its recording has no audio track, the RTSP restream and the
// plugins' frame feeds carry none of its sound, and nothing may talk through its speaker. A change takes effect when
// the camera's session starts again, which the stream keeper does at once (audio_policy_restarts), so the next
// segment follows it.
//
// The cloud plugin sends the whole policy: each camera it knows, and default_audio for any other (one added before
// the cloud has heard of it). This computer keeps the last one it got in config/audio.json, for when the cloud is out
// of reach. One never told records every camera's audio, as before.
struct r_audio_policy
{
    bool default_audio {true};
    std::map<std::string, bool> cameras;

    R_API bool records_audio(const std::string& camera_id) const;
    R_API bool operator==(const r_audio_policy& other) const { return default_audio == other.default_audio && cameras == other.cameras; }
};

// {"default_audio": bool, "cameras": {"<camera id>": bool, ...}}. Null for anything else, so a malformed request
// changes nothing.
R_API r_utils::r_nullable<r_audio_policy> parse_audio_policy(const std::string& json_text);
R_API std::string audio_policy_json(const r_audio_policy& policy);

// config/audio.json under top_dir. Missing or unreadable: every camera's audio is recorded.
R_API r_audio_policy load_audio_policy(const std::string& top_dir);
R_API bool save_audio_policy(const std::string& top_dir, const r_audio_policy& policy);

// The running cameras (id, and whether its session records audio) whose session must start again to follow the
// policy.
R_API std::set<std::string> audio_policy_restarts(const std::map<std::string, bool>& running, const r_audio_policy& policy);

}

#endif
