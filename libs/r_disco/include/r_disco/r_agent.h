
#ifndef r_disco_r_agent_h
#define r_disco_r_agent_h

#include "r_disco/r_stream_config.h"
#include "r_utils/r_timer.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include "r_disco/providers/r_onvif_provider.h"
#include <thread>
#include <vector>
#include <memory>
#include <map>
#include <mutex>

namespace r_disco
{

// - All cameras come from discovery. Manually added cameras still come from a discovery
//   provider (so that all the cameras work the same way), just a special one that is only
//   for manually entered cameras.
//
// - discover onvif cameras
//   for each camera
//     populate stream config structs from RTSP DESCRIBE + onvif info
//     generate stream config hash
//       - camera id is GUID, current IP address is just another stream config attribute
//       - onvif may have a GUID somewhere but if it doesn't, it looks like you can read /proc/net/arp to get HW addresses of recent connections
//

typedef std::function<void(const std::vector<std::pair<r_stream_config, std::string>>&)> changed_streams_cb;
typedef std::function<std::pair<r_utils::r_nullable<std::string>, r_utils::r_nullable<std::string>>(const std::string&)> credential_cb;
typedef std::function<bool(const std::string&)> is_recording_cb;

class r_agent
{
    friend class r_onvif_provider;

public:
    R_API r_agent(const std::string& top_dir);
    R_API ~r_agent() noexcept;

    R_API void set_stream_change_cb(changed_streams_cb cb) {_changed_streams_cb = cb;}
    R_API void set_credential_cb(credential_cb cb) {_credential_cb = cb;}
    R_API void set_is_recording_cb(is_recording_cb cb) {_is_recording_cb = cb;}

    R_API void start();
    R_API void stop();
    R_API void interrogate_camera(
        const std::string& camera_name,
        const std::string& ipv4,
        const std::string& xaddrs,
        const std::string& address,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password
    );

    R_API void forget(const std::string& id);

private:
    std::pair<r_utils::r_nullable<std::string>, r_utils::r_nullable<std::string>> _get_credentials(const std::string& id);
    bool _is_recording(const std::string& id);
    void _entry_point();
    void _process_new_or_changed_streams_configs();

    std::thread _th;
    bool _running;
    std::unique_ptr<r_onvif_provider> _onvif_provider;
    changed_streams_cb _changed_streams_cb;
    std::string _top_dir;
    r_utils::r_timer _timer;
    std::mutex _device_config_hashes_mutex;
    std::map<std::string, std::string> _device_config_hashes;
    credential_cb _credential_cb;
    is_recording_cb _is_recording_cb;
};

}

#endif
