#include "r_http/r_websocket_client.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"
#include <chrono>

using namespace r_http;
using namespace r_utils;

r_websocket_client::r_websocket_client(std::unique_ptr<r_socket_base> socket)
    : _socket(std::move(socket))
    , _connected(true)
    , _send_mutex()
    , _receive_thread()
    , _keepalive_thread()
    , _on_message()
{
    if (!_socket || !_socket->valid())
        R_THROW(("Invalid socket provided to WebSocket client"));
}

r_websocket_client::~r_websocket_client()
{
    stop();
}

void r_websocket_client::handshake(const std::string& host, const std::string& path)
{
    if (!_socket || !_socket->valid())
        R_THROW(("Socket not connected"));

    r_websocket::perform_client_handshake(*_socket, host, path);
}

void r_websocket_client::set_message_callback(message_callback callback)
{
    _on_message = callback;
}

void r_websocket_client::send_text(const std::string& message)
{
    if (!_connected)
        R_THROW(("WebSocket not connected"));

    std::lock_guard<std::mutex> lock(_send_mutex);
    r_websocket::send_text(*_socket, message, true);  // Client must mask
}

void r_websocket_client::send_binary(const std::vector<uint8_t>& data)
{
    if (!_connected)
        R_THROW(("WebSocket not connected"));

    std::lock_guard<std::mutex> lock(_send_mutex);
    r_websocket::send_binary(*_socket, data, true);  // Client must mask
}

void r_websocket_client::close(ws_close_code code, const std::string& reason)
{
    if (!_connected)
        return;

    _connected = false;

    try {
        std::lock_guard<std::mutex> lock(_send_mutex);
        r_websocket::send_close(*_socket, code, reason, true);  // Client must mask
    } catch (const r_exception& ex) {
        R_LOG_EXCEPTION(ex);
    }

    stop();
}

bool r_websocket_client::is_connected() const
{
    return _connected;
}

void r_websocket_client::start()
{
    if (!_connected)
        R_THROW(("Cannot start - not connected"));

    // Start receive thread
    _receive_thread = std::thread(&r_websocket_client::_receive_loop, this);

    // Start keepalive thread
    _keepalive_thread = std::thread(&r_websocket_client::_keepalive_loop, this);
}

void r_websocket_client::stop()
{
    _connected = false;

    // Close socket first to unblock any pending recv() calls in threads
    if (_socket && _socket->valid())
        _socket->close();

    // Join threads (they will exit once they detect socket closed or _connected == false)
    if (_receive_thread.joinable())
        _receive_thread.join();

    if (_keepalive_thread.joinable())
        _keepalive_thread.join();
}

void r_websocket_client::_receive_loop()
{
    try {
    while (_connected) {
        try {
            // Receive frame
            auto frame = r_websocket::recv_frame(*_socket);

            // Handle control frames
            if (frame.opcode == ws_opcode::ping) {
                // Respond with pong
                std::lock_guard<std::mutex> lock(_send_mutex);
                r_websocket::send_pong(*_socket, frame.payload, true);  // Client must mask
            }
            else if (frame.opcode == ws_opcode::pong) {
                // Pong received - connection alive
                // (no action needed)
            }
            else if (frame.opcode == ws_opcode::close) {
                // Server wants to close
                try {
                    std::lock_guard<std::mutex> lock(_send_mutex);
                    r_websocket::send_close(*_socket, ws_close_code::normal, "", true);
                } catch (...) {}

                _connected = false;
                break;
            }
            else {
                // Data frame - call user callback
                if (_on_message) {
                    try {
                        _on_message(frame);
                    } catch (const std::exception& ex) {
                        R_LOG_ERROR("Exception in message callback: %s", ex.what());
                    }
                }
            }
        }
        catch (const r_exception& ex) {
            // Socket error or protocol violation (expected during shutdown)
            if (_connected) {
                // Only log if we weren't intentionally disconnecting
                R_LOG_EXCEPTION(ex);
            }
            _connected = false;
            break;
        }
        catch (const std::exception& ex) {
            // Standard exception
            if (_connected) {
                R_LOG_ERROR("Exception in WebSocket receive loop: %s", ex.what());
            }
            _connected = false;
            break;
        }
        catch (...) {
            // Unknown exception
            if (_connected) {
                R_LOG_ERROR("Unknown exception in WebSocket receive loop");
            }
            _connected = false;
            break;
        }
    }
    } catch (...) {
        // Absolute last resort - catch any exception that escaped inner handlers
        // This prevents std::terminate() from being called
        _connected = false;
    }
}

void r_websocket_client::_keepalive_loop()
{
    try {
    while (_connected) {
        // Wait 30 seconds
        for (int i = 0; i < 30 && _connected; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!_connected)
            break;

        // Send ping
        try {
            std::lock_guard<std::mutex> lock(_send_mutex);
            r_websocket::send_ping(*_socket, std::vector<uint8_t>(), true);  // Client must mask
        }
        catch (const r_exception& ex) {
            // Failed to send ping - connection likely dead
            R_LOG_EXCEPTION(ex);
            _connected = false;
            break;
        }
    }
    } catch (...) {
        // Absolute last resort - catch any exception that escaped inner handlers
        _connected = false;
    }
}
