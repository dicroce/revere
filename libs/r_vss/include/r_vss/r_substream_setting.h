#ifndef __r_vss_r_substream_setting_h
#define __r_vss_r_substream_setting_h

#include "r_utils/r_macro.h"
#include <atomic>
#include <mutex>
#include <string>

namespace r_vss
{

// Whether this computer uses its cameras' smaller streams (substreams) at all. Off unless it is turned on in
// Settings: until then the smaller stream is not recorded, and live view, grid tiles, "now" pictures, plugin frame
// feeds and motion detection all use each camera's main stream, whatever a camera's own properties say. A camera's
// choice of a smaller stream (set up in its properties) only takes effect while this is on.
//
// config/substreams.json: {"enabled": false}. A missing file, bad JSON or a non-boolean value is off.
R_API bool parse_substream_setting(const std::string& json_text);
R_API std::string substream_setting_json(bool enabled);

class r_substream_setting final
{
public:
    R_API explicit r_substream_setting(const std::string& top_dir);

    R_API bool enabled() const { return _enabled.load(); }
    // Saves the choice (config/substreams.json); true when it changed.
    R_API bool set_enabled(bool enabled);

private:
    std::string _config_dir;
    std::string _path;
    std::atomic<bool> _enabled {false};
    std::mutex _write;
};

}

#endif
