#ifndef r_disco_r_discovery_health_h
#define r_disco_r_discovery_health_h

#include "r_utils/r_macro.h"
#include <cstddef>
#include <mutex>
#include <string>

namespace r_disco
{

// Whether discovery's replies look blocked. A camera answers the multicast WS-Discovery Probe with a unicast reply
// from its port 3702. A host firewall that tracks connections (ufw, firewalld) doesn't match that reply to the
// multicast probe, and drops it. A direct (unicast) probe of a camera's address is answered from the address it was
// sent to, which the firewall does match. So cameras revere already records keep being found by direct probes,
// but a new camera never appears.
//
// Its sign, pass after pass: cameras answer direct probes and none answers multicast. After PASSES_TO_WARN such
// passes in a row it says so, and the first multicast reply clears it. A pass where nothing answered at all says
// nothing either way (no cameras, or all of them off), so a network with no cameras never warns.
//
// Some cheap cameras ignore multicast Probes on their own. A site whose only cameras are those can see the warning
// without a firewall, so it says "may be".
//
// Thread-safe.
class r_discovery_health final
{
public:
    enum { PASSES_TO_WARN = 3 };

    R_API static const char* warning_text();

    // One discovery pass: how many cameras answered the multicast probe, and how many more were found only by a
    // direct probe (a known camera's address, or the subnet sweep). True when replies_blocked() changed.
    R_API bool pass(size_t multicast_found, size_t unicast_only_found);

    R_API bool replies_blocked() const;
    // warning_text() when replies_blocked(), otherwise empty.
    R_API std::string warning() const;

private:
    mutable std::mutex _mtx;
    int _suspect_passes {0};
    bool _blocked {false};
};

}

#endif
