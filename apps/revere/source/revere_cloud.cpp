
#include "revere_cloud.h"
#include "utils.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_ssl_socket.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_http/r_websocket_client.h"
#include "r_http/r_uri.h"
#include <chrono>

using namespace revere;
using namespace std;
using namespace r_utils;
using namespace r_utils::r_networking;
using namespace r_http;

using json = nlohmann::json;

// API Configuration
const string API_HOST = "belfry-backend.scott-6be.workers.dev";
const int API_PORT = 443;  // HTTPS
const chrono::seconds AUTH_TIMEOUT = chrono::seconds(120);
const chrono::seconds HEARTBEAT_INTERVAL = chrono::seconds(60);

// 1) Gateway requests a device code and user code from the API
// 2) Gateway displays the user code and opens a browser (if possible)
// 3) User logs in on the web page and authorizes the device
// 4) Gateway polls the API until user completes authorization
// 5) Gateway receives an access token (JWT)
// 6) Gateway registers itself using the access token
// 7) Gateway receives an API key for ongoing operations

revere_cloud::revere_cloud(configure_state& config)
    : _config(config), _thread(), _running(false), _auth_state(auth_state::NOT_AUTHENTICATED), _auth_start_time(),
      _last_heartbeat_time(), _last_ws_connect_attempt(), _ws_endpoint_unavailable(false)
{
    // Check if we already have an API key from previous session
    if (_has_api_key())
        _auth_state = auth_state::AUTHENTICATED;
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
    while (_running.load())
    {
        // Check if we need to begin authentication
        if (enabled() && _auth_state == auth_state::NOT_AUTHENTICATED)
            _do_begin_authenticate();

        // Continue polling for authorization completion if waiting
        if (_auth_state == auth_state::AWAITING_USER_AUTHORIZATION)
            _do_poll_authenticate();

        // If we are authenticated but cloud is disabled, deauthorize
        if(auth_state::AUTHENTICATED == _auth_state && !enabled())
        {
            R_LOG_INFO("Revere Cloud: Disabled while authenticated - deauthorizing.");
            _do_deauthorize();
        }

        // If authenticated and enabled, manage WebSocket connection and heartbeats
        if (_auth_state == auth_state::AUTHENTICATED && enabled())
        {
            // Connect WebSocket if not connected (with backoff if endpoint unavailable)
            if (!_ws_client || !_ws_client->is_connected())
            {
                auto now = chrono::steady_clock::now();
                auto elapsed_since_attempt = chrono::duration_cast<chrono::seconds>(now - _last_ws_connect_attempt);

                // If endpoint was unavailable, only retry every 5 minutes
                // Otherwise retry every loop iteration
                bool should_retry = !_ws_endpoint_unavailable || elapsed_since_attempt >= chrono::seconds(300);

                if (should_retry)
                {
                    _last_ws_connect_attempt = now;
                    _connect_websocket();
                }
            }

            // Send heartbeat if enough time has elapsed
            auto now = chrono::steady_clock::now();
            auto elapsed = chrono::duration_cast<chrono::seconds>(now - _last_heartbeat_time);
            if (elapsed >= HEARTBEAT_INTERVAL)
            {
                _send_heartbeat();
                _last_heartbeat_time = now;
            }
        }
        else
        {
            // Disconnect WebSocket if not authenticated or disabled
            if (_ws_client)
            {
                _disconnect_websocket();
            }
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

    // If disabling, the background thread will handle cleanup when it detects !enabled()
    // This avoids race conditions with WebSocket cleanup
}

bool revere_cloud::authenticated() const
{
    return _auth_state == auth_state::AUTHENTICATED;
}

bool revere_cloud::_has_api_key() const
{
    auto access_token = _config.get_value("revere_cloud_access_token", "");
    return !access_token.empty();
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

    try
    {
        // Step 1: Request device code and user code from the API
        r_ssl_socket socket(true);  // Enable certificate verification
        socket.connect(API_HOST, API_PORT);

        // Get device information
        string local_ip = socket.get_local_ip();
        string device_uuid = r_networking::r_get_device_uuid("");

        // Check if we have an existing gateway_id (for re-authentication)
        auto existing_gateway_id = _config.get_value("revere_cloud_gateway_id", "");

        R_LOG_INFO("Revere Cloud: Device info - IP: %s, MAC: %s", local_ip.c_str(), device_uuid.c_str());
        if (!existing_gateway_id.empty())
            R_LOG_INFO("Revere Cloud: Re-authenticating existing gateway: %s", existing_gateway_id.c_str());

        // Create JSON request body with device info
        json request_body;
        request_body["device_name"] = "Revere Gateway";
        request_body["ip_address"] = local_ip;
        request_body["mac_address"] = device_uuid;
        request_body["firmware_version"] = "1.0.0";

        // Include gateway_id for re-authentication if we have it
        if (!existing_gateway_id.empty())
            request_body["gateway_id"] = existing_gateway_id;

        string body_str = request_body.dump();

        // Create HTTP request
        r_client_request request(API_HOST, API_PORT);
        request.set_method(METHOD_POST);
        request.set_uri(r_uri("/api/v1/device-auth/initiate"));
        request.set_content_type("application/json");
        request.set_body(body_str);

        // Send request
        request.write_request(socket);

        // Read response
        r_client_response response;
        response.read_response(socket);

        socket.close();

        if (!response.is_success())
        {
            R_LOG_ERROR("Revere Cloud: Failed to initiate device auth. Status: %d", response.get_status());
            _auth_state = auth_state::NOT_AUTHENTICATED;
            return;
        }

        // Parse response
        auto body_str_resp = response.get_body_as_string();
        if (body_str_resp.is_null())
        {
            R_LOG_ERROR("Revere Cloud: Failed to parse response body");
            _auth_state = auth_state::NOT_AUTHENTICATED;
            return;
        }

        json response_json = json::parse(body_str_resp.value());

        // Handle both old format {"success": true, "data": {...}} and new format (direct data)
        json data;
        if (response_json.contains("success") && response_json.contains("data"))
        {
            // Old format with wrapper
            if (!response_json["success"].get<bool>())
            {
                R_LOG_ERROR("Revere Cloud: API returned success=false");
                _auth_state = auth_state::NOT_AUTHENTICATED;
                return;
            }
            data = response_json["data"];
        }
        else
        {
            // New format - data is at root level
            data = response_json;
        }

        _device_code = data["device_code"].get<string>();
        _user_code = data["user_code"].get<string>();
        _verification_uri = data["verification_uri_complete"].get<string>();

        R_LOG_INFO("Revere Cloud: User code: %s", _user_code.c_str());
        R_LOG_INFO("Revere Cloud: Please visit: %s", _verification_uri.c_str());

        // Open browser to the verification URI
        if (open_url_in_browser(_verification_uri))
        {
            R_LOG_INFO("Revere Cloud: Opened browser for authentication");
        }
        else
        {
            R_LOG_WARNING("Revere Cloud: Failed to open browser - please visit URL manually");
        }

        // TODO: Step 2 - Display the user code to the user in the UI

        // Capture the start time for timeout tracking
        _auth_start_time = chrono::steady_clock::now();

        // Move to the awaiting state - _do_poll_authenticate will poll for completion
        _auth_state = auth_state::AWAITING_USER_AUTHORIZATION;
    }
    catch (const exception& ex)
    {
        R_LOG_ERROR("Revere Cloud: Exception during authentication initiation: %s", ex.what());
        _auth_state = auth_state::NOT_AUTHENTICATED;
    }
}

void revere_cloud::_do_poll_authenticate()
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

void revere_cloud::_do_finalize_authenticate()
{
    try
    {
        // Step 4: Poll the API to check if user has completed authorization
        r_ssl_socket socket(true);  // Enable certificate verification
        socket.connect(API_HOST, API_PORT);

        // Create JSON request body
        json request_body;
        request_body["device_code"] = _device_code;

        string body_str = request_body.dump();

        // Create HTTP request
        r_client_request request(API_HOST, API_PORT);
        request.set_method(METHOD_POST);
        request.set_uri(r_uri("/api/v1/device-auth/poll"));
        request.set_content_type("application/json");
        request.set_body(body_str);

        // Send request
        request.write_request(socket);

        // Read response
        r_client_response response;
        response.read_response(socket);

        socket.close();

        // Check for different status codes
        if (response.get_status() == 400)
        {
            // Parse error response
            auto body_str_resp = response.get_body_as_string();
            if (!body_str_resp.is_null())
            {
                json response_json = json::parse(body_str_resp.value());
                string error_code = response_json.value("error", "");

                if (error_code == "authorization_pending")
                {
                    // Still waiting, this is expected
                    return;
                }
                else if (error_code == "expired_token")
                {
                    R_LOG_ERROR("Revere Cloud: Device code expired");
                    _auth_state = auth_state::NOT_AUTHENTICATED;
                    return;
                }
                else if (error_code == "access_denied")
                {
                    R_LOG_ERROR("Revere Cloud: Authorization was denied");
                    _auth_state = auth_state::NOT_AUTHENTICATED;
                    return;
                }
            }

            // Unknown error
            R_LOG_ERROR("Revere Cloud: Polling failed with status 400");
            return;
        }

        if (!response.is_success())
        {
            auto error_body = response.get_body_as_string();
            if (!error_body.is_null())
                R_LOG_ERROR("Revere Cloud: Polling failed. Status: %d, Body: %s", response.get_status(), error_body.value().c_str());
            else
                R_LOG_ERROR("Revere Cloud: Polling failed. Status: %d", response.get_status());
            return;
        }

        // Parse successful response
        auto body_str_resp = response.get_body_as_string();
        if (body_str_resp.is_null())
        {
            R_LOG_ERROR("Revere Cloud: Failed to parse poll response body");
            return;
        }

        json response_json = json::parse(body_str_resp.value());

        // Step 5: We have the access token and gateway info!
        // The response format is: { "access_token": "...", "token_type": "Bearer", "gateway": {...}, "is_reauth": true/false }
        string access_token = response_json["access_token"].get<string>();
        string gateway_id = response_json["gateway"]["id"].get<string>();
        bool is_reauth = response_json.value("is_reauth", false);

        if (is_reauth)
            R_LOG_INFO("Revere Cloud: Re-authentication successful! Gateway ID: %s", gateway_id.c_str());
        else
            R_LOG_INFO("Revere Cloud: Authorization successful! Gateway ID: %s", gateway_id.c_str());

        // Step 6: Save the access token and gateway ID
        _config.set_value("revere_cloud_access_token", access_token);
        _config.set_value("revere_cloud_gateway_id", gateway_id);
        _config.save();

        // Move to authenticated state
        _auth_state = auth_state::AUTHENTICATED;
        R_LOG_INFO("Revere Cloud: Authentication completed successfully. Gateway ID: %s", gateway_id.c_str());
    }
    catch (const exception& ex)
    {
        R_LOG_ERROR("Revere Cloud: Exception during authentication finalization: %s", ex.what());
        // Don't change state here - let it timeout naturally
    }
}

void revere_cloud::_do_deauthorize()
{
    // Disconnect WebSocket first
    _disconnect_websocket();

    // Clear access token to force re-authentication on re-enable
    // Keep gateway_id - it's permanent and tied to this hardware
    _config.set_value("revere_cloud_access_token", "");
    _config.set_bool("revere_cloud_enabled", false);
    _config.save();

    // Reset auth state
    _auth_state = auth_state::NOT_AUTHENTICATED;

    R_LOG_INFO("Revere Cloud: Deauthorized - access token cleared and cloud disabled.");
}

void revere_cloud::_connect_websocket()
{
    try
    {
        auto access_token = _config.get_value("revere_cloud_access_token", "");
        auto gateway_id = _config.get_value("revere_cloud_gateway_id", "");

        if (access_token.empty() || gateway_id.empty())
        {
            R_LOG_WARNING("Revere Cloud: Cannot connect WebSocket - missing credentials");
            return;
        }

        R_LOG_INFO("Revere Cloud: Connecting to WebSocket...");

        // Create SSL socket
        auto socket = make_unique<r_ssl_socket>(true);
        socket->connect(API_HOST, API_PORT);

        // Create WebSocket client
        _ws_client = make_unique<r_websocket_client>(std::move(socket));

        // Perform handshake with gateway_id in path (no auth needed per docs)
        string ws_path = "/api/v1/gateways/" + gateway_id + "/ws";
        _ws_client->handshake(API_HOST, ws_path);

        // Set message callback
        _ws_client->set_message_callback([this](const r_websocket_frame& frame) {
            _on_websocket_message(frame);
        });

        // Start background threads
        _ws_client->start();

        R_LOG_INFO("Revere Cloud: WebSocket connected successfully");
    }
    catch (const exception& ex)
    {
        string error_msg = ex.what();

        // Check if this is a 404 error (endpoint not implemented)
        if (error_msg.find("404") != string::npos)
        {
            _ws_endpoint_unavailable = true;
            R_LOG_WARNING("Revere Cloud: WebSocket endpoint not available (404) - will retry in 5 minutes");
        }
        else
        {
            _ws_endpoint_unavailable = false;
            R_LOG_ERROR("Revere Cloud: WebSocket connection failed: %s", error_msg.c_str());
        }

        _ws_client.reset();
    }
}

void revere_cloud::_disconnect_websocket()
{
    if (_ws_client)
    {
        R_LOG_INFO("Revere Cloud: Disconnecting WebSocket...");
        _ws_client->close();
        _ws_client.reset();
    }
}

void revere_cloud::_send_heartbeat()
{
    try
    {
        auto access_token = _config.get_value("revere_cloud_access_token", "");
        auto gateway_id = _config.get_value("revere_cloud_gateway_id", "");

        if (access_token.empty() || gateway_id.empty())
        {
            R_LOG_WARNING("Revere Cloud: Cannot send heartbeat - missing credentials");
            return;
        }

        R_LOG_INFO("Revere Cloud: Sending heartbeat for gateway %s with access token %s...", gateway_id.c_str(), access_token.c_str());

        // Create SSL socket for HTTP request
        r_ssl_socket socket(true);
        socket.connect(API_HOST, API_PORT);

        // Create heartbeat request
        r_client_request request(API_HOST, API_PORT);
        request.set_method(METHOD_POST);
        request.set_uri(r_uri("/api/v1/gateways/" + gateway_id + "/heartbeat"));
        request.add_header("Authorization", "Bearer " + access_token);

        // Send request
        request.write_request(socket);

        // Read response
        r_client_response response;
        response.read_response(socket);

        socket.close();

        if (response.is_success())
        {
            R_LOG_INFO("Revere Cloud: Heartbeat sent successfully");
        }
        else
        {
            auto error_body = response.get_body_as_string();
            if (!error_body.is_null())
                R_LOG_WARNING("Revere Cloud: Heartbeat failed with status %d, Body: %s", response.get_status(), error_body.value().c_str());
            else
                R_LOG_WARNING("Revere Cloud: Heartbeat failed with status %d", response.get_status());
        }
    }
    catch (const exception& ex)
    {
        R_LOG_ERROR("Revere Cloud: Heartbeat error: %s", ex.what());
    }
}

void revere_cloud::_on_websocket_message(const r_websocket_frame& frame)
{
    try
    {
        if (frame.opcode == ws_opcode::text)
        {
            string message = frame.get_payload_as_string();
            R_LOG_INFO("Revere Cloud: WebSocket message: %s", message.c_str());

            if(frame.is_control_frame())
            {
                // Parse JSON message
                json msg = json::parse(message);

                // Handle application-level ping messages
                if (msg.contains("type") && msg["type"] == "ping")
                {
                    // Respond with pong
                    json pong_msg;
                    pong_msg["type"] = "pong";
                    pong_msg["timestamp"] = msg.value("timestamp", "");

                    _ws_client->send_text(pong_msg.dump());
                    R_LOG_INFO("Revere Cloud: Responded to ping with pong");
                }
            }
            else if(frame.is_data_frame())
            {
                // Handle data frames (application-specific)
                R_LOG_INFO("Revere Cloud: Received WebSocket data frame: %s", message.c_str());
            }
            else
            {
                R_LOG_WARNING("Revere Cloud: Received unknown WebSocket text frame");
            }
        }
    }
    catch (const exception& ex)
    {
        R_LOG_ERROR("Revere Cloud: Error handling WebSocket message: %s", ex.what());
    }
}
