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
//
// Who may set it (audio_policy_set_allowed): once the cloud plugin has set it (with the local token), the computer is
// cloud-managed (managed_by "cloud") and a person signed in can't change it, since an organization may turn audio off
// for legal reasons. On a computer with no cloud, a person signed in sets it.
struct r_audio_policy
{
    bool default_audio {true};
    std::map<std::string, bool> cameras;
    bool cloud_managed {false};

    R_API bool records_audio(const std::string& camera_id) const;
    R_API bool operator==(const r_audio_policy& other) const
    {
        return default_audio == other.default_audio && cameras == other.cameras && cloud_managed == other.cloud_managed;
    }
};

// {"default_audio": bool, "cameras": {"<camera id>": bool, ...}, "managed_by": "cloud" | "local"}. managed_by is
// optional (absent is "local"). Null for anything else, so a malformed request changes nothing.

// Whether a request may replace the current policy: the plugin's local token always; a person signed in only while
// the cloud doesn't manage it.
R_API bool audio_policy_set_allowed(const r_audio_policy& current, bool from_local_token);
// The policy as kept: managed by the cloud when the plugin set it, by this computer when a person did, whatever the
// request said.
R_API r_audio_policy audio_policy_as_set(r_audio_policy requested, bool from_local_token);
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
