
#ifndef __vision_ptz_controls_h
#define __vision_ptz_controls_h

#include "imgui/imgui.h"
#include "ptz_query.h"
#include "timeline_constants.h"
#include "r_ui_utils/theme.h"
#include "r_ui_utils/font_catalog.h"
#include "font_keys.h"
#include "r_utils/r_logger.h"
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
// mid-move we still send a stop.

namespace ptz_detail
{

enum class support { unknown, probing, yes, no };

struct cam_entry
{
    support sup {support::unknown};
    std::vector<ptz_preset> presets;
    bool supports_zoom {false};   // camera's PTZ node has a continuous zoom axis
    bool moving {false};          // a direction button was held last frame
};

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
inline void _fire(std::string host, std::string camera_id, std::string action,
                  float pan, float tilt, float zoom, std::string preset_token)
{
    struct job { std::string host, camera_id, action; float pan, tilt, zoom; std::string preset_token; };
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
        try
        {
            probe = query_ptz(host, camera_id, "list_presets", 0.0f, 0.0f, 0.0f, std::string());
            result = support::yes;
        }
        catch(...) { result = support::no; }

        std::lock_guard<std::mutex> g(_mtx());
        auto& e = _cams()[camera_id];
        e.sup = result;
        e.presets = probe.presets;
        e.supports_zoom = probe.supports_zoom;
    }).detach();
}

// A press-and-hold direction button: start a continuous move while held, stop
// on release. Sets any_active while the button is down.
inline void _dir_button(const char* label, const char* id,
                        const std::string& host, const std::string& camera_id,
                        float pan, float tilt, float zoom, bool& any_active,
                        ImVec2 size = ImVec2(42, 42))
{
    ImGui::PushID(id);
    ImGui::Button(label, size);
    if(ImGui::IsItemActivated())
        _fire(host, camera_id, "move", pan, tilt, zoom, std::string());
    if(ImGui::IsItemDeactivated())
        _fire(host, camera_id, "stop", 0.0f, 0.0f, 0.0f, std::string());
    if(ImGui::IsItemActive())
        any_active = true;
    ImGui::PopID();
}

inline void _draw_pad(const std::string& host, const std::string& camera_id,
                      const std::vector<ptz_preset>& presets, bool supports_zoom)
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

    // Zoom row (ONVIF Zoom velocity): held to zoom, released to stop. Shown
    // only when the camera's PTZ node advertises a continuous zoom axis -- a
    // zoom command to a camera without one is a silent no-op, so the buttons
    // would just sit there doing nothing. Release sends stop, which halts
    // pan/tilt and zoom alike.
    if(supports_zoom)
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
    bool supports_zoom = false;
    {
        std::lock_guard<std::mutex> g(_mtx());
        auto it = _cams().find(camera_id);
        if(it == _cams().end())
        {
            _cams()[camera_id].sup = support::probing;
            sup = support::unknown;   // launch the probe below, outside the lock
        }
        else
        {
            sup = it->second.sup;
            presets = it->second.presets;
            supports_zoom = it->second.supports_zoom;
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
        _draw_pad(host, camera_id, presets, supports_zoom);
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
