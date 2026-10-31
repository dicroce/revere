
#ifndef __vision_ptz_controls_h
#define __vision_ptz_controls_h

#include "imgui/imgui.h"
#include "ptz_query.h"
#include "timeline_constants.h"
#include "r_ui_utils/theme.h"
#include "r_ui_utils/font_catalog.h"
#include "font_keys.h"
#include "r_utils/r_logger.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vision
{

// PTZ controls for the live camera, shown in the control bar's right group and
// shared by every Vision surface that draws a control bar (the grid and the
// single-camera overlay). The button, and its D-pad / presets popup, appear
// only for a camera that actually exposes ONVIF PTZ: the first time a camera is
// seen we probe Revere's /ptz (list_presets) on a background thread and cache
// the yes/no answer for the session. A fixed camera never grows a PTZ button.
//
// Every /ptz request runs off the UI thread (commands on one ordered worker,
// the probe on its own thread) so the HTTP round trip never stalls the render
// loop. A dropped move/stop isn't worth interrupting the
// viewer, so failures are logged, not surfaced. The camera is told to move
// while a direction is held and to stop on release; if the popup is dismissed
// mid-move we still send a stop. Each move also stops by itself
// (PTZ_MOVE_TIMEOUT) and is re-sent while held, so a stop that's lost, or
// Vision quitting mid-hold, can't leave the camera moving. A probe that
// couldn't reach Revere is tried again after PROBE_RETRY_S.

namespace ptz_detail
{

enum class support { unknown, probing, yes, no };

struct cam_entry
{
    support sup {support::unknown};
    std::vector<ptz_preset> presets;
    bool show_zoom {true};        // false only when the camera says it has no continuous zoom axis
    bool moving {false};          // a direction button was held last frame
    // A probe that failed without an answer (Revere starting, or briefly
    // unreachable): when it may be asked again. Unset for a camera without PTZ.
    std::chrono::steady_clock::time_point retry_at {};
};

// A failed probe is asked again this long after, not left for the session.
constexpr int PROBE_RETRY_S = 60;

inline std::mutex& _mtx() { static std::mutex m; return m; }
inline std::map<std::string, cam_entry>& _cams() { static std::map<std::string, cam_entry> m; return m; }

// Normalized pan/tilt/zoom velocity for the D-pad (ONVIF ContinuousMove, [-1,1]).
constexpr float PTZ_SPEED = 0.6f;
constexpr float PTZ_ZOOM_SPEED = 0.6f;

// Fire a PTZ command and forget it: the UI thread must not block on the camera.
// Commands run one at a time, in the order sent, on a single worker thread: a
// quick tap's stop must not overtake its move (a thread per command would race
// for Revere's ONVIF lock, and a stop that ran first would leave the camera
// moving). The queue is never destroyed, so the detached worker can't outlive it.
// Commands sent and not yet answered; a keep-alive waits while any are.
inline std::atomic<int>& _queued() { static auto* n = new std::atomic<int>(0); return *n; }

inline void _fire(std::string host, std::string camera_id, std::string action,
                  float pan, float tilt, float zoom, std::string preset_token)
{
    struct job { std::string host, camera_id, action; float pan, tilt, zoom; std::string preset_token; };
    ++_queued();
    static auto* m = new std::mutex;
    static auto* cv = new std::condition_variable;
    static auto* q = new std::deque<job>;
    static bool started = false;

    {
        std::lock_guard<std::mutex> g(*m);
        q->push_back({std::move(host), std::move(camera_id), std::move(action), pan, tilt, zoom, std::move(preset_token)});
        if(!started)
        {
            started = true;
            std::thread([]() {
                for(;;)
                {
                    job j;
                    {
                        std::unique_lock<std::mutex> l(*m);
                        cv->wait(l, []() { return !q->empty(); });
                        j = std::move(q->front());
                        q->pop_front();
                    }
                    try { query_ptz(j.host, j.camera_id, j.action, j.pan, j.tilt, j.zoom, j.preset_token); }
                    catch(const std::exception& ex) { R_LOG_WARNING("vision: PTZ %s failed: %s", j.action.c_str(), ex.what()); }
                    --_queued();
                }
            }).detach();
        }
    }
    cv->notify_one();
}

// Probe PTZ support (and presets) for a camera, once, on a background thread.
inline void _probe(std::string host, std::string camera_id)
{
    std::thread([=]() {
        support result;
        ptz_result probe;
        bool retry = false;
        try
        {
            probe = query_ptz(host, camera_id, "list_presets", 0.0f, 0.0f, 0.0f, std::string());
            result = support::yes;
        }
        catch(const ptz_unsupported&) { result = support::no; }
        catch(...) { result = support::no; retry = true; }

        std::lock_guard<std::mutex> g(_mtx());
        auto& e = _cams()[camera_id];
        e.sup = result;
        e.presets = probe.presets;
        // Unknown keeps the zoom row; only an explicit "no zoom" hides it.
        e.show_zoom = probe.supports_zoom.value_or(true);
        e.retry_at = retry ? std::chrono::steady_clock::now() + std::chrono::seconds(PROBE_RETRY_S)
                           : std::chrono::steady_clock::time_point{};
    }).detach();
}

// A press-and-hold direction button: start a continuous move while held, stop
// on release. Sets any_active while the button is down.
inline void _dir_button(const char* label, const char* id,
                        const std::string& host, const std::string& camera_id,
                        float pan, float tilt, float zoom, bool& any_active,
                        ImVec2 size = ImVec2(42, 42))
{
    // When the held button last sent its move (one button is held at a time).
    static std::chrono::steady_clock::time_point last_move;
    ImGui::PushID(id);
    ImGui::Button(label, size);
    auto now = std::chrono::steady_clock::now();
    if(ImGui::IsItemActivated())
    {
        _fire(host, camera_id, "move", pan, tilt, zoom, std::string());
        last_move = now;
    }
    else if(ImGui::IsItemActive() && now - last_move >= std::chrono::milliseconds(PTZ_KEEPALIVE_MS) && _queued().load() == 0)
    {
        // Still held: the move goes again before its timeout ends it. Not while
        // a command is still on its way, so a slow Revere builds no queue.
        _fire(host, camera_id, "move", pan, tilt, zoom, std::string());
        last_move = now;
    }
    if(ImGui::IsItemDeactivated())
        _fire(host, camera_id, "stop", 0.0f, 0.0f, 0.0f, std::string());
    if(ImGui::IsItemActive())
        any_active = true;
    ImGui::PopID();
}

inline void _draw_pad(const std::string& host, const std::string& camera_id,
                      const std::vector<ptz_preset>& presets, bool show_zoom)
{
    ImFont* f = r_ui_utils::fonts[vision::get_font_key_16()].inter_tight_regular;
    if(f) ImGui::PushFont(f);

    bool any_active = false;

    // Three rows:   .  up  .   /   left stop right   /   .  down  .
    ImGui::Dummy(ImVec2(42, 42)); ImGui::SameLine();
    _dir_button("^", "up", host, camera_id, 0.0f, ptz_detail::PTZ_SPEED, 0.0f, any_active);

    _dir_button("<", "left", host, camera_id, -ptz_detail::PTZ_SPEED, 0.0f, 0.0f, any_active);
    ImGui::SameLine();
    if(ImGui::Button("||", ImVec2(42, 42)))
        _fire(host, camera_id, "stop", 0.0f, 0.0f, 0.0f, std::string());
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("Stop");
    ImGui::SameLine();
    _dir_button(">", "right", host, camera_id, ptz_detail::PTZ_SPEED, 0.0f, 0.0f, any_active);

    ImGui::Dummy(ImVec2(42, 42)); ImGui::SameLine();
    _dir_button("v", "down", host, camera_id, 0.0f, -ptz_detail::PTZ_SPEED, 0.0f, any_active);

    // Zoom row (ONVIF Zoom velocity): held to zoom, released to stop. Hidden
    // when the camera's PTZ node says it has no continuous zoom axis -- a zoom
    // command to it is a silent no-op -- and shown when it didn't say. Release
    // sends stop, which halts pan/tilt and zoom alike.
    if(show_zoom)
    {
        _dir_button("Zoom -", "zoomout", host, camera_id, 0.0f, 0.0f, -ptz_detail::PTZ_ZOOM_SPEED, any_active, ImVec2(63, 30));
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom out");
        ImGui::SameLine();
        _dir_button("Zoom +", "zoomin", host, camera_id, 0.0f, 0.0f, ptz_detail::PTZ_ZOOM_SPEED, any_active, ImVec2(63, 30));
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom in");
    }

    {
        std::lock_guard<std::mutex> g(_mtx());
        _cams()[camera_id].moving = any_active;
    }

    if(!presets.empty())
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Presets");
        for(size_t i = 0; i < presets.size(); ++i)
        {
            const auto& p = presets[i];
            std::string label = p.name.empty() ? p.token : p.name;
            ImGui::PushID((int)i);
            if(ImGui::Button(label.c_str(), ImVec2(150, 0)))
                _fire(host, camera_id, "goto_preset", 0.0f, 0.0f, 0.0f, p.token);
            ImGui::PopID();
        }
    }

    if(f) ImGui::PopFont();
}

}

// Draw the PTZ button into the control row's right group, advancing right_x
// leftward like the other right-group buttons. Draws nothing unless the camera
// is known to support PTZ; probes it lazily the first time it is seen. host is
// the Revere the camera belongs to; both host and camera_id must be non-empty.
inline void ptz_control_button(const std::string& host, const std::string& camera_id,
                               float row_top, float& right_x)
{
    using namespace ptz_detail;

    if(host.empty() || camera_id.empty())
        return;

    support sup;
    std::vector<ptz_preset> presets;
    bool show_zoom = true;
    {
        std::lock_guard<std::mutex> g(_mtx());
        auto it = _cams().find(camera_id);
        bool retry_due = it != _cams().end() && it->second.sup == support::no
                         && it->second.retry_at != std::chrono::steady_clock::time_point{}
                         && std::chrono::steady_clock::now() >= it->second.retry_at;
        if(it == _cams().end() || retry_due)
        {
            _cams()[camera_id].sup = support::probing;
            sup = support::unknown;   // launch the probe below, outside the lock
        }
        else
        {
            sup = it->second.sup;
            presets = it->second.presets;
            show_zoom = it->second.show_zoom;
        }
    }

    if(sup == support::unknown)
    {
        _probe(host, camera_id);
        return;
    }
    if(sup != support::yes)
        return;   // still probing, or no PTZ on this camera: draw nothing

    const float gap = timeline_constants::ROW_BUTTON_GAP;

    right_x -= 80.0f;
    ImGui::SetCursorScreenPos(ImVec2(right_x, row_top));
    if(ImGui::Button("PTZ", ImVec2(80, 0)))
        ImGui::OpenPopup("##ptz_popover");
    if(ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##ptz_popover"))
        ImGui::SetTooltip("Pan, tilt and zoom this camera");

    // Opens upward and to the left, anchored to the button's right edge: the
    // button sits in the bar's right group near the window edge, so growing
    // rightward would push the pad (its right arrow first) off-screen.
    ImGui::SetNextWindowPos(ImVec2(right_x + 80.0f, row_top - 6.0f), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    if(ImGui::BeginPopup("##ptz_popover"))
    {
        _draw_pad(host, camera_id, presets, show_zoom);
        ImGui::EndPopup();
    }
    else
    {
        // The popup was dismissed. If a direction was held when it closed, the
        // button's release may not have fired, so make sure the camera stops.
        bool was_moving = false;
        {
            std::lock_guard<std::mutex> g(_mtx());
            auto& e = _cams()[camera_id];
            was_moving = e.moving;
            e.moving = false;
        }
        if(was_moving)
            _fire(host, camera_id, "stop", 0.0f, 0.0f, 0.0f, std::string());
    }

    right_x -= gap;
}

}

#endif
