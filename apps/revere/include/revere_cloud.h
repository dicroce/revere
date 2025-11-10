
#ifndef __revere_cloud_h
#define __revere_cloud_h

#include "configure_state.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

namespace r_http { class r_websocket_client; class r_websocket_frame; }

namespace revere
{

enum class auth_state
{
    NOT_AUTHENTICATED,
    AWAITING_USER_AUTHORIZATION,
    AUTHENTICATED
};

class revere_cloud
{
public:
    revere_cloud(configure_state& config);
    ~revere_cloud();

    void start();
    void stop();

    bool enabled() const;
    void set_enabled(bool enabled);

    bool authenticated() const;

private:
    void _run();
    void _do_begin_authenticate();
    void _do_poll_authenticate();
    void _do_finalize_authenticate();
    void _do_deauthorize();
    bool _has_api_key() const;

    // WebSocket and heartbeat functions
    void _connect_websocket();
    void _disconnect_websocket();
    void _send_heartbeat();
    void _on_websocket_message(const r_http::r_websocket_frame& frame);

    configure_state& _config;
    std::thread _thread;
    std::atomic<bool> _running;
    auth_state _auth_state;
    std::chrono::steady_clock::time_point _auth_start_time;
    std::chrono::steady_clock::time_point _last_heartbeat_time;
    std::chrono::steady_clock::time_point _last_ws_connect_attempt;
    std::string _device_code;
    std::string _user_code;
    std::string _verification_uri;
    std::unique_ptr<r_http::r_websocket_client> _ws_client;
    bool _ws_endpoint_unavailable;
};

}

#endif
