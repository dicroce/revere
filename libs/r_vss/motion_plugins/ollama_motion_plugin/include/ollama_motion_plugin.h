#ifndef __ollama_motion_plugin_h
#define __ollama_motion_plugin_h

#include "r_vss/r_motion_plugin.h"
#include "r_utils/r_macro.h"
#include "r_utils/r_blocking_q.h"
#include <atomic>

namespace r_vss {
    class r_motion_event_plugin_host;
}
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct OllamaSession {
    std::string camera_id;
    std::string camera_name;
    int64_t start_ts;
    int64_t last_event_ts;
    std::vector<uint8_t> best_frame_data;
    uint16_t best_frame_width;
    uint16_t best_frame_height;
    uint32_t best_frame_bbox_area;
    r_vss::motion_region best_motion_bbox;
};

struct CameraSchedulerState {
    double ema_rate_per_ms   = 1.0 / 86400000.0;
    double next_service_time = 0.0;
    int64_t last_session_end_ts = 0;
    std::optional<OllamaSession> open_session;
    std::optional<OllamaSession> pending_session;
};

struct OllamaConfig {
    std::string host       = "127.0.0.1";
    int port               = 11434;
    //std::string model      = "llava";
    std::string model        = "qwen3-vl";
    int64_t session_gap_ms = 5000;
    double ema_alpha       = 0.2;
//    std::string prompt     = "Describe what you see in this security camera image. "
//                             "Is there a person, vehicle, animal, or nothing notable? Be brief.";
    std::string prompt     = "Please describe what you see using the following class labels when applicable: 'PERSON', "
                             "'CHILD', 'CAR', 'TRUCK', 'VAN', 'DELIVERY VEHICLE', 'ANIMAL', 'WEAPON', 'SCHOOL BUS', 'TRASH BIN', 'EMPLOYEE', "
                             "'CUSTOMER', 'POLICE OFFICER', 'SECURITY GUARD', 'FIREFIGHTER', 'FIGHT', 'THEFT', 'VANDALISM', 'BURGLARY'. "
                             "If you don't see any of those respond with 'NONE'.";
};

struct IncomingEvent {
    r_vss::r_motion_event evt;
    std::string camera_id;
    int64_t ts;
    std::vector<uint8_t> frame_data;
    uint16_t width;
    uint16_t height;
    r_vss::motion_region motion_bbox;
};

class ollama_motion_plugin : public r_vss::r_motion_plugin
{
public:
    R_API ollama_motion_plugin(r_vss::r_motion_event_plugin_host* host);
    R_API virtual ~ollama_motion_plugin();
    R_API void stop();

    R_API virtual void post_motion_event(
        r_vss::r_motion_event evt,
        const std::string& camera_id,
        int64_t ts,
        const std::vector<uint8_t>& frame_data,
        uint16_t width, uint16_t height,
        const r_vss::motion_region& motion_bbox) override;

private:
    r_vss::r_motion_event_plugin_host* _host;
    OllamaConfig _config;
    std::atomic<bool> _running;

    r_utils::r_blocking_q<IncomingEvent> _event_queue;

    std::mutex _camera_states_mutex;
    std::condition_variable _scheduler_cv;
    std::map<std::string, CameraSchedulerState> _camera_states;

    std::thread _event_thread;
    std::thread _timer_thread;
    std::thread _inference_thread;

    void _event_entry_point();
    void _timer_entry_point();
    void _inference_entry_point();

    void _process_event(const IncomingEvent& evt);
    void _close_session(const std::string& camera_id, CameraSchedulerState& state);
    void _start_new_session(const std::string& camera_id, CameraSchedulerState& state,
                            const IncomingEvent& evt);

    std::string _query_ollama(const OllamaSession& session);
    void _handle_result(const OllamaSession& session, const std::string& raw_response, double elapsed_secs);
    std::string _encode_frame_to_base64_jpeg(const OllamaSession& session);

    void _load_config();
};

#endif
