#include "ollama_motion_plugin.h"
#include "r_vss/r_motion_event_plugin_host.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_socket.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_http/r_methods.h"
#include "r_http/r_uri.h"
#include "r_utils/3rdparty/json/json.h"
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>

using namespace r_utils;

static int64_t _now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

ollama_motion_plugin::ollama_motion_plugin(r_vss::r_motion_event_plugin_host* host)
    : _host(host),
      _running(true),
      _event_queue(30, r_utils::r_queue_full_policy::drop_newest)
{
    _load_config();
    _event_thread     = std::thread(&ollama_motion_plugin::_event_entry_point, this);
    _timer_thread     = std::thread(&ollama_motion_plugin::_timer_entry_point, this);
    _inference_thread = std::thread(&ollama_motion_plugin::_inference_entry_point, this);
}

ollama_motion_plugin::~ollama_motion_plugin()
{
    stop();
    if (_event_thread.joinable())     _event_thread.join();
    if (_timer_thread.joinable())     _timer_thread.join();
    if (_inference_thread.joinable()) _inference_thread.join();
}

void ollama_motion_plugin::stop()
{
    if (!_running.exchange(false))
        return;
    _event_queue.wake();
    _scheduler_cv.notify_all();
}

void ollama_motion_plugin::post_motion_event(
    r_vss::r_motion_event evt,
    const std::string& camera_id,
    int64_t ts,
    const std::vector<uint8_t>& frame_data,
    uint16_t width, uint16_t height,
    const r_vss::motion_region& motion_bbox)
{
    if (!_running)
        return;

    IncomingEvent ev;
    ev.evt        = evt;
    ev.camera_id  = camera_id;
    ev.ts         = ts;
    ev.frame_data = frame_data;
    ev.width      = width;
    ev.height     = height;
    ev.motion_bbox = motion_bbox;

    _event_queue.post(ev);
}

void ollama_motion_plugin::_event_entry_point()
{
    while (_running) {
        try {
            auto maybe_evt = _event_queue.poll();
            if (!_running)
                break;
            if (!maybe_evt.is_null())
                _process_event(maybe_evt.value());
        } catch (const std::exception& ex) {
            R_LOG_ERROR("ollama_motion_plugin: event thread error: %s", ex.what());
        }
    }
}

void ollama_motion_plugin::_timer_entry_point()
{
    while (_running) {
        // Sleep in 100ms increments so shutdown is responsive within ~100ms
        for (int i = 0; i < 10 && _running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (!_running)
            break;

        int64_t now = _now_ms();
        std::lock_guard<std::mutex> lk(_camera_states_mutex);
        for (auto& [camera_id, state] : _camera_states) {
            if (state.open_session.has_value()) {
                int64_t age = now - state.open_session->last_event_ts;
                if (age >= _config.session_gap_ms)
                    _close_session(camera_id, state);
            }
        }
    }
}

void ollama_motion_plugin::_inference_entry_point()
{
    while (_running) {
        OllamaSession session;
        bool have_session = false;

        {
            std::unique_lock<std::mutex> lk(_camera_states_mutex);

            std::string best_camera;
            double best_vt = DBL_MAX;
            for (auto& [camera_id, state] : _camera_states) {
                if (state.pending_session.has_value() &&
                    state.next_service_time < best_vt)
                {
                    best_vt = state.next_service_time;
                    best_camera = camera_id;
                }
            }

            if (best_camera.empty()) {
                _scheduler_cv.wait_for(lk, std::chrono::milliseconds(2000));
                continue;
            }

            session = std::move(_camera_states[best_camera].pending_session.value());
            _camera_states[best_camera].pending_session = std::nullopt;
            _camera_states[best_camera].next_service_time +=
                1.0 / _camera_states[best_camera].ema_rate_per_ms;
            have_session = true;
        }

        if (have_session && _running) {
            try {
                auto t0 = std::chrono::steady_clock::now();
                std::string raw = _query_ollama(session);
                double elapsed_secs = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - t0).count();
                if (!raw.empty())
                    _handle_result(session, raw, elapsed_secs);
            } catch (const std::exception& ex) {
                R_LOG_ERROR("ollama_motion_plugin: inference error: %s", ex.what());
            }
        }
    }
}

void ollama_motion_plugin::_process_event(const IncomingEvent& evt)
{
    std::lock_guard<std::mutex> lk(_camera_states_mutex);
    auto& state = _camera_states[evt.camera_id];

    if (state.open_session.has_value()) {
        int64_t gap = evt.ts - state.open_session->last_event_ts;
        if (gap <= _config.session_gap_ms) {
            state.open_session->last_event_ts = evt.ts;
            uint32_t area = (uint32_t)evt.motion_bbox.width * (uint32_t)evt.motion_bbox.height;
            if (area > state.open_session->best_frame_bbox_area) {
                state.open_session->best_frame_data     = evt.frame_data;
                state.open_session->best_frame_width    = evt.width;
                state.open_session->best_frame_height   = evt.height;
                state.open_session->best_frame_bbox_area = area;
                state.open_session->best_motion_bbox    = evt.motion_bbox;
            }
            if (evt.evt == r_vss::motion_event_end)
                _close_session(evt.camera_id, state);
        } else {
            _close_session(evt.camera_id, state);
            _start_new_session(evt.camera_id, state, evt);
        }
    } else {
        if (evt.evt != r_vss::motion_event_end)
            _start_new_session(evt.camera_id, state, evt);
    }
}

void ollama_motion_plugin::_start_new_session(
    const std::string& camera_id,
    CameraSchedulerState& state,
    const IncomingEvent& evt)
{
    OllamaSession s;
    s.camera_id             = camera_id;
    auto maybe_camera = _host->get_devices().get_camera_by_id(camera_id);
    if (!maybe_camera.is_null() && !maybe_camera->friendly_name.is_null())
        s.camera_name = maybe_camera->friendly_name.value();
    s.start_ts              = evt.ts;
    s.last_event_ts         = evt.ts;
    s.best_frame_data       = evt.frame_data;
    s.best_frame_width      = evt.width;
    s.best_frame_height     = evt.height;
    s.best_frame_bbox_area  = (uint32_t)evt.motion_bbox.width * (uint32_t)evt.motion_bbox.height;
    s.best_motion_bbox      = evt.motion_bbox;
    state.open_session      = std::move(s);
}

void ollama_motion_plugin::_close_session(
    const std::string& /*camera_id*/,
    CameraSchedulerState& state)
{
    OllamaSession session = std::move(state.open_session.value());
    state.open_session = std::nullopt;

    if (state.last_session_end_ts > 0) {
        int64_t interval_ms = session.start_ts - state.last_session_end_ts;
        if (interval_ms > 0) {
            double new_rate = 1.0 / (double)interval_ms;
            state.ema_rate_per_ms = _config.ema_alpha * new_rate
                                  + (1.0 - _config.ema_alpha) * state.ema_rate_per_ms;
            constexpr double floor_rate = 1.0 / 86400000.0;
            if (state.ema_rate_per_ms < floor_rate)
                state.ema_rate_per_ms = floor_rate;
        }
    }

    state.last_session_end_ts = session.last_event_ts;

    if (!state.pending_session.has_value()) {
        state.pending_session = std::move(session);
        _scheduler_cv.notify_one();
    }
}

std::string ollama_motion_plugin::_encode_frame_to_base64_jpeg(const OllamaSession& session)
{
    cv::Mat mat(session.best_frame_height, session.best_frame_width,
                CV_8UC3, (void*)session.best_frame_data.data());
    std::vector<uint8_t> jpeg_buf;
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 85};
    cv::imencode(".jpg", mat, jpeg_buf, params);
    return r_utils::r_string_utils::to_base64(jpeg_buf.data(), jpeg_buf.size());
}

std::string ollama_motion_plugin::_query_ollama(const OllamaSession& session)
{
    try {
        std::string b64 = _encode_frame_to_base64_jpeg(session);

        nlohmann::json body = {
            {"model", _config.model},
            {"stream", false},
            {"messages", nlohmann::json::array({
                nlohmann::json{
                    {"role", "user"},
                    {"content", _config.prompt},
                    {"images", nlohmann::json::array({b64})}
                }
            })}
        };
        std::string body_str = body.dump();

        r_utils::r_socket sock;
        sock.connect(_config.host, _config.port);

        r_http::r_client_request req(_config.host, _config.port);
        req.set_method(r_http::METHOD_POST);
        req.set_uri(r_http::r_uri("/api/chat"));
        req.set_content_type("application/json");
        req.set_body(body_str);
        req.write_request(sock, 5000);

        r_http::r_client_response resp;
        resp.read_response(sock, 300000);

        if (resp.is_success()) {
            auto maybe_body = resp.get_body_as_string();
            if (maybe_body.is_null())
                return "";
            auto j = nlohmann::json::parse(maybe_body.value());
            return j["message"]["content"].get<std::string>();
        }
    } catch (const std::exception& ex) {
        R_LOG_WARNING("ollama_motion_plugin: ollama request failed: %s", ex.what());
    }
    return "";
}

void ollama_motion_plugin::_handle_result(const OllamaSession& session, const std::string& raw, double elapsed_secs)
{
    std::string clean = raw;
    std::replace(clean.begin(), clean.end(), '\n', ' ');
    std::replace(clean.begin(), clean.end(), '\r', ' ');
    std::replace(clean.begin(), clean.end(), '\t', ' ');

    const std::string& display_name = session.camera_name.empty() ? session.camera_id : session.camera_name;
    R_LOG_INFO("ollama_motion_plugin [%s] %lld-%lld (%.1fs): %s",
               display_name.c_str(),
               (long long)session.start_ts,
               (long long)session.last_event_ts,
               elapsed_secs,
               clean.c_str());

    std::string log_path = _host->get_top_dir() + "/ollama_events.log";
    std::ofstream ofs(log_path, std::ios::app);
    if (ofs) {
        ofs << session.start_ts << '\t'
            << session.last_event_ts << '\t'
            << session.camera_id << '\t'
            << clean << '\n';
    }
}

void ollama_motion_plugin::_load_config()
{
    std::string conf_path = _host->get_top_dir() + "/plugins/ollama_motion_plugin.conf";

    std::ifstream ifs(conf_path);
    if (!ifs.is_open()) {
        std::ofstream ofs(conf_path);
        if (ofs) {
            ofs << "ollama_host=" << _config.host << "\n"
                << "ollama_port=" << _config.port << "\n"
                << "ollama_model=" << _config.model << "\n"
                << "session_gap_ms=" << _config.session_gap_ms << "\n"
                << "ema_alpha=" << _config.ema_alpha << "\n"
                << "prompt=" << _config.prompt << "\n";
        }
        return;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if      (key == "ollama_host")      _config.host = val;
        else if (key == "ollama_port")      _config.port = std::stoi(val);
        else if (key == "ollama_model")     _config.model = val;
        else if (key == "session_gap_ms")   _config.session_gap_ms = std::stoll(val);
        else if (key == "ema_alpha")        _config.ema_alpha = std::stod(val);
        else if (key == "prompt")           _config.prompt = val;
    }
}

extern "C"
{

R_API r_motion_plugin_handle load_plugin(r_motion_event_plugin_host_handle host_handle)
{
    auto* host = reinterpret_cast<r_vss::r_motion_event_plugin_host*>(host_handle);
    return reinterpret_cast<r_motion_plugin_handle>(new ollama_motion_plugin(host));
}

R_API void stop_plugin(r_motion_plugin_handle plugin_handle)
{
    reinterpret_cast<ollama_motion_plugin*>(plugin_handle)->stop();
}

R_API void destroy_plugin(r_motion_plugin_handle plugin_handle)
{
    delete reinterpret_cast<ollama_motion_plugin*>(plugin_handle);
}

R_API void post_motion_event(
    r_motion_plugin_handle plugin_handle,
    int evt, const char* camera_id, int64_t ts,
    const uint8_t* frame_data, size_t frame_data_size,
    uint16_t width, uint16_t height,
    int motion_x, int motion_y, int motion_width, int motion_height,
    bool has_motion)
{
    auto* plugin = reinterpret_cast<ollama_motion_plugin*>(plugin_handle);
    r_vss::motion_region bbox{motion_x, motion_y, motion_width, motion_height, has_motion};
    plugin->post_motion_event(
        static_cast<r_vss::r_motion_event>(evt),
        camera_id, ts,
        std::vector<uint8_t>(frame_data, frame_data + frame_data_size),
        width, height, bbox);
}

}
