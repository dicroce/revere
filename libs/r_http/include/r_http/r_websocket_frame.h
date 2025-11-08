#ifndef r_http_r_websocket_frame_h
#define r_http_r_websocket_frame_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <vector>
#include <string>

namespace r_http
{

// WebSocket opcodes (RFC 6455 Section 5.2)
enum class ws_opcode : uint8_t
{
    continuation = 0x0,
    text = 0x1,
    binary = 0x2,
    // 0x3-0x7 reserved for future non-control frames
    close = 0x8,
    ping = 0x9,
    pong = 0xA
    // 0xB-0xF reserved for future control frames
};

// WebSocket close status codes (RFC 6455 Section 7.4.1)
enum class ws_close_code : uint16_t
{
    normal = 1000,              // Normal closure
    going_away = 1001,          // Endpoint going away
    protocol_error = 1002,      // Protocol error
    unsupported_data = 1003,    // Received unsupported data type
    no_status_received = 1005,  // No status code (MUST NOT be sent)
    abnormal_closure = 1006,    // Abnormal closure (MUST NOT be sent)
    invalid_frame_payload = 1007, // Invalid UTF-8 or inconsistent data
    policy_violation = 1008,    // Generic policy violation
    message_too_big = 1009,     // Message too big to process
    mandatory_ext = 1010,       // Missing required extension
    internal_error = 1011,      // Unexpected condition
    tls_handshake = 1015        // TLS handshake failure (MUST NOT be sent)
};

// WebSocket frame structure
struct r_websocket_frame
{
    bool fin;                           // Final fragment flag
    uint8_t rsv1;                       // Reserved bit 1 (for extensions)
    uint8_t rsv2;                       // Reserved bit 2
    uint8_t rsv3;                       // Reserved bit 3
    ws_opcode opcode;                   // Frame opcode
    bool masked;                        // Mask flag
    std::vector<uint8_t> payload;       // Frame payload data

    // Constructor
    R_API r_websocket_frame();

    // Convenience constructor for simple frames
    R_API r_websocket_frame(ws_opcode op, const std::vector<uint8_t>& data, bool fin_flag = true);

    // Helper methods
    R_API bool is_control_frame() const;
    R_API bool is_data_frame() const;
    R_API std::string get_payload_as_string() const;
};

// Frame parsing and building functions

// Parse a WebSocket frame from socket data
// Throws r_exception on protocol errors
R_API r_websocket_frame parse_websocket_frame(const std::vector<uint8_t>& data);

// Build a WebSocket frame for sending
// mask_frame: true for client->server, false for server->client
R_API std::vector<uint8_t> build_websocket_frame(
    ws_opcode opcode,
    const std::vector<uint8_t>& payload,
    bool fin = true,
    bool mask_frame = false
);

// Build a WebSocket frame from a frame struct
R_API std::vector<uint8_t> build_websocket_frame(const r_websocket_frame& frame);

// Mask/unmask payload data (XOR operation, same function for both)
R_API void mask_payload(
    std::vector<uint8_t>& payload,
    const uint8_t masking_key[4]
);

// Generate random masking key (for client frames)
R_API void generate_masking_key(uint8_t masking_key[4]);

} // namespace r_http

#endif
