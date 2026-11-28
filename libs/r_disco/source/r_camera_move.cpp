#include "r_disco/r_camera_move.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/3rdparty/json/json.h"
#include <chrono>

using namespace r_disco;
using namespace r_utils;
using namespace std;

r_nullable<r_camera_move> r_disco::camera_move_from(const r_camera& stored, const r_stream_config& fresh)
{
    r_nullable<r_camera_move> none;
    if(stored.state != "assigned" || stored.ipv4.is_null())
        return none;

    auto differs = [](const auto& stored_field, const auto& fresh_field) {
        return !fresh_field.is_null() && (stored_field.is_null() || stored_field.value() != fresh_field.value());
    };
    if(!differs(stored.ipv4, fresh.ipv4) && !differs(stored.onvif_port, fresh.port) && !differs(stored.xaddrs, fresh.xaddrs))
        return none;

    r_camera_move m;
    m.old_ipv4 = stored.ipv4.value();
    m.new_ipv4 = fresh.ipv4.is_null() ? m.old_ipv4 : fresh.ipv4.value();
    m.old_port = stored.onvif_port.is_null() ? 0 : stored.onvif_port.value();
    m.new_port = fresh.port.is_null() ? m.old_port : fresh.port.value();
    m.old_xaddrs = stored.xaddrs.is_null() ? string() : stored.xaddrs.value();
    m.new_xaddrs = fresh.xaddrs.is_null() ? m.old_xaddrs : fresh.xaddrs.value();
    m.seen_at_ms = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    r_nullable<r_camera_move> result;
    result.set_value(m);
    return result;
}

string r_disco::camera_move_message(const r_camera_move& move)
{
    auto where = [](const string& ip, int port) {
        return port > 0 ? ip + ":" + to_string(port) : ip;
    };
    auto change = where(move.old_ipv4, move.old_port) + " -> " + where(move.new_ipv4, move.new_port);
    if(move.status == "followed")
    {
        string mac;
        if(move.mac_check == "changed")
            mac = " The device there has a different hardware (MAC) address, so check it's the same camera.";
        else if(move.mac_check == "unknown")
            mac = " Its hardware (MAC) address couldn't be read there, so check it's the same camera.";
        else if(move.mac_check == "none")
            mac = " There was no hardware (MAC) address on file to check it's the same camera.";
        return "Camera moved: " + change + ". Recording continues at the new address." + mac;
    }
    string why;
    if(move.hold_reason == "no_stored_mac")
        why = " There is no hardware (MAC) address on file to check it's the same camera. Confirming sends the camera's login to it.";
    else if(move.hold_reason == "mac_unknown")
        why = " Its hardware (MAC) address couldn't be read there to check it's the same camera. Confirming sends the camera's login to it.";
    else if(move.hold_reason == "mac_mismatch")
        why = " A different device answers there (its hardware (MAC) address isn't the camera's). Confirming sends the camera's login to it.";
    return "Camera moved? It answers discovery at a new address: " + change + "." + why +
           " Confirm the new address, or keep the current one.";
}

string r_disco::hold_reason_for(const r_nullable<string>& stored_mac, const r_nullable<string>& new_mac)
{
    auto a = stored_mac.is_null() ? r_nullable<string>() : r_utils::r_networking::r_normalize_mac(stored_mac.value());
    if(a.is_null())
        return "no_stored_mac";
    auto b = new_mac.is_null() ? r_nullable<string>() : r_utils::r_networking::r_normalize_mac(new_mac.value());
    if(b.is_null())
        return "mac_unknown";
    if(a.value() != b.value())
        return "mac_mismatch";
    return string();
}

move_outcome r_disco::decide_camera_move(const r_nullable<string>& stored_mac, const r_nullable<string>& new_mac)
{
    return hold_reason_for(stored_mac, new_mac).empty() ? move_outcome::follow : move_outcome::hold;
}

string r_disco::mac_check_for(const string& hold_reason)
{
    if(hold_reason.empty())
        return "match";
    if(hold_reason == "mac_mismatch")
        return "changed";
    if(hold_reason == "mac_unknown")
        return "unknown";
    return "none";
}

bool r_disco::parse_camera_move_policy(const string& json_text)
{
    auto j = nlohmann::json::parse(json_text, nullptr, false);
    return j.is_object() && j.contains("follow_unverified_moves") && j["follow_unverified_moves"].is_boolean() &&
           j["follow_unverified_moves"].get<bool>();
}

string r_disco::camera_move_policy_json(bool follow_unverified_moves)
{
    return nlohmann::json{{"follow_unverified_moves", follow_unverified_moves}}.dump();
}

static string _policy_path(const string& top_dir)
{
    return top_dir + PATH_SLASH + "config" + PATH_SLASH + "camera_moves.json";
}

bool r_disco::load_camera_move_policy(const string& top_dir)
{
    auto path = _policy_path(top_dir);
    try
    {
        if(!r_fs::file_exists(path))
            return false;
        auto buf = r_fs::read_file(path);
        return parse_camera_move_policy(string((char*)buf.data(), buf.size()));
    }
    catch(const exception& ex)
    {
        R_LOG_WARNING("Camera moves: couldn't read %s, so a camera at a new address is held until a person confirms: %s", path.c_str(), ex.what());
        return false;
    }
}

bool r_disco::save_camera_move_policy(const string& top_dir, bool follow_unverified_moves)
{
    auto path = _policy_path(top_dir);
    auto txt = camera_move_policy_json(follow_unverified_moves);
    try
    {
        r_fs::mkdir_p(top_dir + PATH_SLASH + "config");
        r_fs::atomic_write_file((const uint8_t*)txt.c_str(), txt.size(), path);
        return true;
    }
    catch(const exception& ex)
    {
        R_LOG_ERROR("Camera moves: couldn't save %s: %s", path.c_str(), ex.what());
        return false;
    }
}

std::map<string, string> r_disco::parse_unconfirmed_addresses(const string& json_text)
{
    std::map<string, string> out;
    auto j = nlohmann::json::parse(json_text, nullptr, false);
    if(!j.is_object())
        return out;
    for(auto it = j.begin(); it != j.end(); ++it)
        if(it.value().is_string())
            out[it.key()] = it.value().get<string>();
    return out;
}

string r_disco::unconfirmed_addresses_json(const std::map<string, string>& addresses)
{
    nlohmann::json j = nlohmann::json::object();
    for(const auto& [id, ipv4] : addresses)
        j[id] = ipv4;
    return j.dump();
}

static string _unconfirmed_path(const string& top_dir)
{
    return top_dir + PATH_SLASH + "config" + PATH_SLASH + "camera_moves_unconfirmed.json";
}

std::map<string, string> r_disco::load_unconfirmed_addresses(const string& top_dir)
{
    auto path = _unconfirmed_path(top_dir);
    try
    {
        if(!r_fs::file_exists(path))
            return {};
        auto buf = r_fs::read_file(path);
        return parse_unconfirmed_addresses(string((char*)buf.data(), buf.size()));
    }
    catch(const exception& ex)
    {
        R_LOG_WARNING("Camera moves: couldn't read %s: %s", path.c_str(), ex.what());
        return {};
    }
}

bool r_disco::save_unconfirmed_addresses(const string& top_dir, const std::map<string, string>& addresses)
{
    auto path = _unconfirmed_path(top_dir);
    auto txt = unconfirmed_addresses_json(addresses);
    try
    {
        r_fs::mkdir_p(top_dir + PATH_SLASH + "config");
        r_fs::atomic_write_file((const uint8_t*)txt.c_str(), txt.size(), path);
        return true;
    }
    catch(const exception& ex)
    {
        R_LOG_ERROR("Camera moves: couldn't save %s: %s", path.c_str(), ex.what());
        return false;
    }
}
