#include "r_disco/r_address_check.h"
#include "r_utils/r_socket.h"

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
