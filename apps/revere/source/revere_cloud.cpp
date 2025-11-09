
#include "revere_cloud.h"
#include "r_utils/r_logger.h"
#include <chrono>

using namespace revere;
using namespace std;

// 1) Gateway requests a device code and user code from the API
// 2) Gateway displays the user code and opens a browser (if possible)
// 3) User logs in on the web page and authorizes the device
// 4) Gateway polls the API until user completes authorization
// 5) Gateway receives an access token (JWT)
// 6) Gateway registers itself using the access token
// 7) Gateway receives an API key for ongoing operations

revere_cloud::revere_cloud(configure_state& config)
    : _config(config), _running(false), _need_authenticate(false), _need_deauthorize(false), _auth_state(auth_state::NOT_AUTHENTICATED)
{
    // Check if we already have an API key from previous session
    if (_has_api_key())
    {
        _auth_state = auth_state::AUTHENTICATED;
    }
}

revere_cloud::~revere_cloud()
{
    stop();
}

void revere_cloud::start()
{
    if (!_running.load())
    {
        _running.store(true);
        _thread = thread(&revere_cloud::_run, this);
    }
}

void revere_cloud::stop()
{
    if (_running.load())
    {
        _running.store(false);
        if (_thread.joinable())
            _thread.join();
    }
}

void revere_cloud::_run()
{
    const auto AUTH_TIMEOUT = chrono::seconds(30);

    while (_running.load())
    {
        // Check if cloud is enabled and we're not authenticated
        if (enabled() && _auth_state == auth_state::NOT_AUTHENTICATED)
        {
            // Automatically trigger authentication if enabled but not authenticated
            _need_authenticate.store(true);
        }

        // Check if we need to begin authentication
        if (_need_authenticate.load() && _auth_state == auth_state::NOT_AUTHENTICATED)
        {
            _need_authenticate.store(false);
            _do_begin_authenticate();
        }

        // Continue polling for authorization completion if waiting
        if (_auth_state == auth_state::AWAITING_USER_AUTHORIZATION)
        {
            // Check for timeout
            auto elapsed = chrono::steady_clock::now() - _auth_start_time;
            if (elapsed >= AUTH_TIMEOUT)
            {
                R_LOG_INFO("Revere Cloud: Authentication timeout - reverting to NOT_AUTHENTICATED state.");
                _auth_state = auth_state::NOT_AUTHENTICATED;
            }
            else
            {
                _do_finalize_authenticate();

                // Check again if we timed out during finalize
                if (_auth_state == auth_state::AWAITING_USER_AUTHORIZATION)
                {
                    elapsed = chrono::steady_clock::now() - _auth_start_time;
                    if (elapsed >= AUTH_TIMEOUT)
                    {
                        R_LOG_INFO("Revere Cloud: Authentication timeout - reverting to NOT_AUTHENTICATED state.");
                        _auth_state = auth_state::NOT_AUTHENTICATED;
                    }
                }
            }
        }

        // Check if we need to deauthorize
        if (_need_deauthorize.load())
        {
            _need_deauthorize.store(false);
            _do_deauthorize();
        }

        // Sleep at the bottom of the loop
        this_thread::sleep_for(chrono::milliseconds(250));
    }
}

bool revere_cloud::enabled() const
{
    return _config.get_bool("revere_cloud_enabled", false);
}

void revere_cloud::set_enabled(bool enabled)
{
    _config.set_bool("revere_cloud_enabled", enabled);
    _config.save();

    // If disabling cloud, also deauthorize
    if (!enabled && authenticated())
    {
        _need_deauthorize.store(true);
    }
}

bool revere_cloud::authenticated() const
{
    return _auth_state == auth_state::AUTHENTICATED;
}

bool revere_cloud::_has_api_key() const
{
    auto api_key = _config.get_value("revere_cloud_api_key", "");
    return !api_key.empty();
}

void revere_cloud::_do_begin_authenticate()
{
    // Check if we already have an API key
    if (_has_api_key())
    {
        R_LOG_INFO("Revere Cloud: Already authenticated with API key.");
        _auth_state = auth_state::AUTHENTICATED;
        return;
    }

    R_LOG_INFO("Revere Cloud: Beginning authentication process...");

    // TODO: Step 1 - Request device code and user code from the API
    // Example: POST /api/v1/auth/device/code
    // Response: { "device_code": "...", "user_code": "...", "verification_uri": "..." }

    // TODO: Step 2 - Display the user code to the user in the UI

    // TODO: Step 3 - Open browser to the verification URI
    // Example: open browser to https://cloud.revere.com/device?user_code=ABCD-1234

    R_LOG_INFO("Revere Cloud: User code: [PLACEHOLDER] - Please authorize in your browser.");

    // Capture the start time for timeout tracking
    _auth_start_time = chrono::steady_clock::now();

    // Move to the awaiting state - _do_finalize_authenticate will poll for completion
    _auth_state = auth_state::AWAITING_USER_AUTHORIZATION;
}

void revere_cloud::_do_finalize_authenticate()
{
    // TODO: Step 4 - Poll the API to check if user has completed authorization
    // Example: POST /api/v1/auth/device/token with device_code
    // If still pending: { "status": "pending" }
    // If authorized: { "access_token": "..." }

    // For now, just log that we're polling
    static int poll_count = 0;
    poll_count++;

    if (poll_count < 13)
    {
        R_LOG_INFO("Revere Cloud: Polling for user authorization... (attempt %d)", poll_count);
        return; // Still waiting
    }

    R_LOG_INFO("Revere Cloud: Simulating successful authorization (placeholder).");

    // TODO: Step 5 - Receive access token (JWT) from successful authorization

    // TODO: Step 6 - Register device using the access token
    // Example: POST /api/v1/devices/register with access_token
    // Response: { "device_id": "...", "device_name": "..." }

    // TODO: Step 7 - Receive API key for ongoing operations
    // Response: { "api_key": "..." }

    // Placeholder - would be replaced with actual API key from auth flow
    // _config.set_value("revere_cloud_api_key", "obtained_api_key_from_server");
    // _config.save();

    // Reset poll count for next authentication
    poll_count = 0;

    // Move to authenticated state
    _auth_state = auth_state::AUTHENTICATED;
    R_LOG_INFO("Revere Cloud: Authentication completed successfully.");
}

void revere_cloud::_do_deauthorize()
{
    // Delete the API key
    _config.set_value("revere_cloud_api_key", "");
    _config.set_bool("revere_cloud_enabled", false);
    _config.save();

    // Reset auth state
    _auth_state = auth_state::NOT_AUTHENTICATED;

    R_LOG_INFO("Revere Cloud: Deauthorized - API key deleted and cloud disabled.");
}
