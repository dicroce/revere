#include "r_cameras/r_camera_events.h"
#include "r_cameras/r_camera_events_chain.h"
#include "r_cameras/onvif/r_onvif_events.h"
#include "r_cameras/reolink/r_reolink_api.h"
#include "r_cameras/reolink/r_reolink_events.h"

using namespace r_cameras;
using namespace std;

bool r_cameras::camera_events_wanted(const r_disco::r_camera& camera)
{
    return !camera.ipv4.is_null() && !camera.ipv4.value().empty() &&
           !camera.xaddrs.is_null() && !camera.xaddrs.value().empty();
}

unique_ptr<r_camera_events> r_cameras::make_camera_events(const r_disco::r_camera& camera, r_camera_events::transition_fn on_transition)
{
    if (!camera_events_wanted(camera))
        return nullptr;

    // ONVIF first; Reolink's own API when ONVIF stops answering and the camera
    // turns out to be a Reolink (its API answers a poll).
    auto tag = camera.friendly_name.is_null() ? camera.id : camera.friendly_name.value();
    return make_unique<r_camera_events_chain>(
        [camera](r_camera_events::transition_fn t) -> unique_ptr<r_camera_events> { return make_unique<r_onvif_events>(camera, std::move(t)); },
        [camera](r_camera_events::transition_fn t) -> unique_ptr<r_camera_events> { return make_unique<r_reolink_events>(camera, std::move(t)); },
        [camera]() {
            try
            {
                r_reolink_client(camera.ipv4.value(), camera.rtsp_username, camera.rtsp_password).poll();
                return true;
            }
            catch (...)
            {
                return false;
            }
        },
        std::move(on_transition), tag);
}
