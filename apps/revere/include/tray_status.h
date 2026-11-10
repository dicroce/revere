#ifndef __revere_tray_status_h
#define __revere_tray_status_h

#include <optional>
#include <string>

namespace revere
{

// What the tray companion shows about the running Revere service.
struct tray_status
{
    bool reachable {false};
    bool problem {false};
    std::string summary;   // e.g. "Recording 2 of 3 cameras"
    std::string detail;    // shown after the summary when there is something to add, else empty
};

// Build the status from the body of GET /cameras. std::nullopt means the
// service could not be reached at all.
tray_status summarize_cameras(const std::optional<std::string>& cameras_json);

// GET /cameras was refused (401/403): the service is up, but this process
// couldn't authenticate, e.g. its local API token is in another user's or
// another --data-dir's data directory.
tray_status summarize_refused();

}

#endif
