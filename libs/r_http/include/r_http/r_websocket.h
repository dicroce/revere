#ifndef r_http_r_websocket_h
#define r_http_r_websocket_h

#include "r_utils/r_macro.h"
#include "r_http/r_websocket_frame.h"
#include "r_utils/interfaces/r_socket_base.h"
#include <string>
#include <vector>
#include <cstdint>

namespace r_http
{

// WebSocket protocol implementation
// All methods operate on existing sockets that have completed HTTP upgrade
class r_websocket
{
public:
    // Send text message (UTF-8)
    R_API static void send_text(
        r_utils::r_socket_base& sock,
        const std::string& message,
        bool mask_frame = false
    );

    // Send binary message
    R_API static void send_binary(
        r_utils::r_socket_base& sock,
        const std::vector<uint8_t>& data,
        bool mask_frame = false
    );

    // Send ping frame (keepalive)
    R_API static void send_ping(
        r_utils::r_socket_base& sock,
        const std::vector<uint8_t>& payload = std::vector<uint8_t>(),
        bool mask_frame = false
    );

    // Send pong frame (response to ping)
    R_API static void send_pong(
        r_utils::r_socket_base& sock,
        const std::vector<uint8_t>& payload,
        bool mask_frame = false
    );

    // Send close frame
    R_API static void send_close(
        r_utils::r_socket_base& sock,
        ws_close_code code = ws_close_code::normal,
        const std::string& reason = "",
        bool mask_frame = false
    );

    // Receive a WebSocket frame
    // This method reads from the socket until a complete frame is received
    // Throws r_exception on socket errors or protocol violations
    R_API static r_websocket_frame recv_frame(
        r_utils::r_socket_base& sock
    );

    // Compute WebSocket accept key for handshake response (server-side)
    // Takes the Sec-WebSocket-Key from client and returns Sec-WebSocket-Accept value
    R_API static std::string compute_accept_key(
        const std::string& websocket_key
    );

    // Generate random WebSocket key for client handshake
    R_API static std::string generate_websocket_key();

    // Perform client-side WebSocket handshake
    // Sends upgrade request and validates server response
    // Throws r_exception on handshake failure
    R_API static void perform_client_handshake(
        r_utils::r_socket_base& sock,
        const std::string& host,
        const std::string& path = "/"
    );

private:
    // Internal helper to send a frame
    static void send_frame(
        r_utils::r_socket_base& sock,
        const r_websocket_frame& frame
    );

    // Internal helper to read exact number of bytes from socket
    static void read_exact(
        r_utils::r_socket_base& sock,
        uint8_t* buffer,
        size_t length
    );
};

} // namespace r_http

#endif
