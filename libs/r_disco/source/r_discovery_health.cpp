#include "r_disco/r_discovery_health.h"

using namespace r_disco;
using namespace std;

const char* r_discovery_health::warning_text()
{
    return "Cameras answer when Revere asks them directly, but none answers network discovery: discovery replies may "
           "be blocked by a firewall, so new cameras won't appear. Allow incoming UDP from port 3702.";
}

bool r_discovery_health::pass(size_t multicast_found, size_t unicast_only_found)
{
    lock_guard<mutex> g(_mtx);
    auto was = _blocked;
    if(multicast_found > 0)
    {
        _suspect_passes = 0;
        _blocked = false;
    }
    else if(unicast_only_found > 0)
    {
        if(_suspect_passes < PASSES_TO_WARN)
            ++_suspect_passes;
        if(_suspect_passes >= PASSES_TO_WARN)
            _blocked = true;
    }
    return _blocked != was;
}

bool r_discovery_health::replies_blocked() const
{
    lock_guard<mutex> g(_mtx);
    return _blocked;
}

string r_discovery_health::warning() const
{
    return replies_blocked() ? string(warning_text()) : string();
}
