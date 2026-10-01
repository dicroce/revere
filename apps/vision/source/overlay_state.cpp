
#include "overlay_state.h"
#include "utils.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/3rdparty/json/json.h"

#include <cctype>

using namespace vision;
using namespace r_utils;
using namespace std;

using json = nlohmann::json;

// Camera ids are uuids today; sanitize anyway so an unexpected id can't
// produce an invalid filename.
static string _state_path(const string& camera_id)
{
    string safe = camera_id;
    for(auto& c : safe)
    {
        if(!isalnum((unsigned char)c) && c != '-' && c != '_')
            c = '_';
    }
    return sub_dir("config") + "overlay_" + safe + ".json";
}

r_nullable<overlay_state> vision::load_overlay_state(const string& camera_id)
{
    r_nullable<overlay_state> output;

    try
    {
        auto path = _state_path(camera_id);
        if(!r_fs::file_exists(path))
            return output;

        auto buffer = r_fs::read_file(path);
        auto j = json::parse(string((char*)buffer.data(), buffer.size()));

        overlay_state os;
        os.x = j.at("x").get<int>();
        os.y = j.at("y").get<int>();
        os.w = j.at("w").get<int>();
        os.h = j.at("h").get<int>();
        os.pinned = j.value("pinned", false);

        // An implausible size (hand-edited file, or garbage from a partial
        // write) is treated the same as no file at all.
        if(os.w < 160 || os.h < 90 || os.w > 16384 || os.h > 16384)
            return output;

        output.set_value(os);
    }
    catch(const std::exception& e)
    {
        R_LOG_WARNING("overlay: ignoring unreadable state for camera %s: %s",
            camera_id.c_str(), e.what());
    }

    return output;
}

void vision::save_overlay_state(const string& camera_id, const overlay_state& os)
{
    try
    {
        json j;
        j["x"] = os.x;
        j["y"] = os.y;
        j["w"] = os.w;
        j["h"] = os.h;
        j["pinned"] = os.pinned;

        auto txt = j.dump();
        r_fs::write_file((uint8_t*)txt.c_str(), txt.size(), _state_path(camera_id));
    }
    catch(const std::exception& e)
    {
        R_LOG_WARNING("overlay: failed to save state for camera %s: %s",
            camera_id.c_str(), e.what());
    }
}
