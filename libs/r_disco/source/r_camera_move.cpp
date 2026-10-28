#include "r_disco/r_camera_move.h"
#include "r_utils/r_socket.h"
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
        return "Camera moved: " + change + ". Recording continues at the new address.";
    return "Camera moved? It answers discovery at a new address: " + change +
           ". Confirm the new address, or keep the current one.";
}

move_outcome r_disco::decide_camera_move(bool old_alive, chrono::milliseconds old_dead_for,
                                         const r_nullable<string>& stored_mac, const r_nullable<string>& new_mac)
{
    if(old_alive || old_dead_for < MOVE_FOLLOW_AFTER)
        return move_outcome::hold;
    if(stored_mac.is_null() || new_mac.is_null())
        return move_outcome::hold;
    auto a = r_utils::r_networking::r_normalize_mac(stored_mac.value());
    auto b = r_utils::r_networking::r_normalize_mac(new_mac.value());
    if(a.is_null() || b.is_null() || a.value() != b.value())
        return move_outcome::hold;
    return move_outcome::follow;
}
