#include "r_disco/r_address_check.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/3rdparty/json/json.h"

using namespace r_disco;
using namespace r_utils;
using namespace std;

bool r_disco::mac_differs(const r_nullable<string>& stored, const r_nullable<string>& seen)
{
    if(stored.is_null() || seen.is_null())
        return false;
    auto a = r_networking::r_normalize_mac(stored.value());
    auto b = r_networking::r_normalize_mac(seen.value());
    if(a.is_null() || b.is_null())
        return false;
    return a.value() != b.value();
}

string r_disco::address_check_message()
{
    return "A different device answers at this camera's address (its hardware (MAC) address isn't the camera's). "
           "Recording is stopped so the camera's login isn't sent to it. Confirm that it is your camera to carry on.";
}

map<string, r_address_hold_record> r_disco::parse_address_holds(const string& json_text)
{
    map<string, r_address_hold_record> out;
    auto j = nlohmann::json::parse(json_text, nullptr, false);
    if(!j.is_object())
        return out;
    for(auto it = j.begin(); it != j.end(); ++it)
    {
        const auto& v = it.value();
        if(!v.is_object() || !v.contains("ipv4") || !v["ipv4"].is_string() || !v.contains("since_ms") || !v["since_ms"].is_number_integer() ||
           !v.contains("seen_mac") || !v["seen_mac"].is_string())
            continue;
        r_address_hold_record r;
        r.ipv4 = v["ipv4"].get<string>();
        r.since_ms = v["since_ms"].get<int64_t>();
        r.seen_mac = v["seen_mac"].get<string>();
        out[it.key()] = r;
    }
    return out;
}

string r_disco::address_holds_json(const map<string, r_address_hold_record>& holds)
{
    nlohmann::json j = nlohmann::json::object();
    for(const auto& [id, h] : holds)
        j[id] = {{"ipv4", h.ipv4}, {"since_ms", h.since_ms}, {"seen_mac", h.seen_mac}};
    return j.dump();
}

static string _holds_path(const string& top_dir)
{
    return top_dir + PATH_SLASH + "config" + PATH_SLASH + "address_holds.json";
}

map<string, r_address_hold_record> r_disco::load_address_holds(const string& top_dir)
{
    auto path = _holds_path(top_dir);
    try
    {
        if(!r_fs::file_exists(path))
            return {};
        auto buf = r_fs::read_file(path);
        return parse_address_holds(string((char*)buf.data(), buf.size()));
    }
    catch(const exception& ex)
    {
        R_LOG_WARNING("Address checks: couldn't read %s: %s", path.c_str(), ex.what());
        return {};
    }
}

bool r_disco::save_address_holds(const string& top_dir, const map<string, r_address_hold_record>& holds)
{
    auto path = _holds_path(top_dir);
    auto txt = address_holds_json(holds);
    try
    {
        r_fs::mkdir_p(top_dir + PATH_SLASH + "config");
        r_fs::atomic_write_file((const uint8_t*)txt.c_str(), txt.size(), path);
        return true;
    }
    catch(const exception& ex)
    {
        R_LOG_ERROR("Address checks: couldn't save %s: %s", path.c_str(), ex.what());
        return false;
    }
}
