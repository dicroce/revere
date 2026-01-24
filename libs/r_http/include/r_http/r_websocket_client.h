#ifndef r_http_r_websocket_client_h
#define r_http_r_websocket_client_h

#include "r_utils/r_macro.h"
#include "r_http/r_websocket.h"
#include "r_utils/interfaces/r_socket_base.h"
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>

namespace r_http
{

// WebSocket client connection wrapper
// Manages connection lifecycle, ping/pong, and message handling
class r_websocket_client
{
public:
    // Callback for received messages
    using message_callback = std::function<void(const r_websocket_frame&)>;

    // Constructor - takes ownership of connected socket
    // Socket must already be connected to remote host
    R_API r_websocket_client(std::unique_ptr<r_utils::r_socket_base> socket);

    // Destructor - closes connection gracefully
    R_API ~r_websocket_client();

    // Perform WebSocket handshake
    R_API void handshake(const std::string& host, const std::string& path = "/");

    // Set callback for received messages
    R_API void set_message_callback(message_callback callback);

    // Send text message
    R_API void send_text(const std::string& message);

    // Send binary message
    R_API void send_binary(const std::vector<uint8_t>& data);

    // Close connection gracefully
    R_API void close(ws_close_code code = ws_close_code::normal, const std::string& reason = "");

    // Check if connection is alive
    R_API bool is_connected() const;

    // Start background threads for receiving and keepalive
    R_API void start();

    // Stop background threads
    R_API void stop();

private:
    void _receive_loop();
    void _keepalive_loop();

    std::unique_ptr<r_utils::r_socket_base> _socket;
    std::atomic<bool> _connected;
    std::mutex _send_mutex;
    std::thread _receive_thread;
    std::thread _keepalive_thread;
    message_callback _on_message;
};

} // namespace r_http

#endif
