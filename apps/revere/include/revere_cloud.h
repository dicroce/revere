
#ifndef __revere_cloud_h
#define __revere_cloud_h

#include "configure_state.h"
#include <thread>
#include <atomic>
#include <chrono>

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
    void _do_finalize_authenticate();
    void _do_deauthorize();
    bool _has_api_key() const;

    configure_state& _config;
    std::thread _thread;
    std::atomic<bool> _running;
    std::atomic<bool> _need_authenticate;
    std::atomic<bool> _need_deauthorize;
    auth_state _auth_state;
    std::chrono::steady_clock::time_point _auth_start_time;
};

}

#endif
