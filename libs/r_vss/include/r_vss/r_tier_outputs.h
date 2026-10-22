
#ifndef __r_vss_r_tier_outputs_h
#define __r_vss_r_tier_outputs_h

#include "r_vss/r_motion_event_plugin_host.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <string>
#include <vector>

namespace r_vss
{

// What tiers 2 and 3 write and put on the detection bus, in the shapes the
// YOLO plugin uses, so Revere Cloud's alerts, the stats and the UIs take them
// unchanged.

// A camera-reported class as the YOLO class name consumers group by:
// person -> person, vehicle -> car, animal -> dog; "" for classes no consumer
// knows (package, visitor, face), which are logged but not reported.
R_API std::string camera_class_to_yolo(const std::string& what);

// Whether a YOLO class is one consumers act on (a person, a vehicle or an
// animal). The pass reports only these: without motion regions to filter by,
// YOLO's other classes (a "bowl" in a porch) are mostly noise.
R_API bool reported_class(const std::string& class_name);

// A box covering the whole 640x640 frame, for camera detections (they carry none).
R_API void whole_frame(r_detection& d);

// The analytics JSON the YOLO plugin writes (boxes normalised from 640x640),
// with "source" naming where it came from ("camera" or "detection_pass").
R_API std::string analytics_json(int64_t start_ms, int64_t end_ms, const std::vector<r_detection>& detections, const std::string& source);

}

#endif
