#include "r_http/r_websocket.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_sha1.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_socket.h"
#include <cstring>

using namespace r_http;
using namespace r_utils;

// WebSocket magic GUID for handshake (RFC 6455)
static const char* WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

void r_websocket::send_text(r_utils::r_socket_base& sock, const std::string& message, bool mask_frame)
{
    std::vector<uint8_t> payload(message.begin(), message.end());
    auto frame_data = build_websocket_frame(ws_opcode::text, payload, true, mask_frame);

    int sent = sock.send(&frame_data[0], frame_data.size());
    if (sent != static_cast<int>(frame_data.size()))
        R_THROW(("Failed to send complete WebSocket text frame"));
}

void r_websocket::send_binary(r_utils::r_socket_base& sock, const std::vector<uint8_t>& data, bool mask_frame)
{
    auto frame_data = build_websocket_frame(ws_opcode::binary, data, true, mask_frame);

    int sent = sock.send(&frame_data[0], frame_data.size());
    if (sent != static_cast<int>(frame_data.size()))
        R_THROW(("Failed to send complete WebSocket binary frame"));
}

void r_websocket::send_ping(r_utils::r_socket_base& sock, const std::vector<uint8_t>& payload, bool mask_frame)
{
    if (payload.size() > 125)
        R_THROW(("Ping payload too large: %zu bytes (max 125)", payload.size()));

    auto frame_data = build_websocket_frame(ws_opcode::ping, payload, true, mask_frame);

    int sent = sock.send(&frame_data[0], frame_data.size());
    if (sent != static_cast<int>(frame_data.size()))
        R_THROW(("Failed to send complete WebSocket ping frame"));
}

void r_websocket::send_pong(r_utils::r_socket_base& sock, const std::vector<uint8_t>& payload, bool mask_frame)
{
    if (payload.size() > 125)
        R_THROW(("Pong payload too large: %zu bytes (max 125)", payload.size()));

    auto frame_data = build_websocket_frame(ws_opcode::pong, payload, true, mask_frame);

    int sent = sock.send(&frame_data[0], frame_data.size());
    if (sent != static_cast<int>(frame_data.size()))
        R_THROW(("Failed to send complete WebSocket pong frame"));
}

void r_websocket::send_close(r_utils::r_socket_base& sock, ws_close_code code, const std::string& reason, bool mask_frame)
{
    std::vector<uint8_t> payload;

    // Add close code (2 bytes, big-endian)
    uint16_t code_val = static_cast<uint16_t>(code);
    payload.push_back(static_cast<uint8_t>((code_val >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(code_val & 0xFF));

    // Add reason (if provided)
    if (!reason.empty()) {
        payload.insert(payload.end(), reason.begin(), reason.end());
    }

    if (payload.size() > 125)
        R_THROW(("Close frame payload too large: %zu bytes (max 125)", payload.size()));

    auto frame_data = build_websocket_frame(ws_opcode::close, payload, true, mask_frame);

    // Note: Don't throw on send failure for close frames
    // The connection may already be half-closed
    sock.send(&frame_data[0], frame_data.size());
}

r_websocket_frame r_websocket::recv_frame(r_utils::r_socket_base& sock)
{
    // Read first 2 bytes (base header)
    uint8_t header[2];
    read_exact(sock, header, 2);

    // Parse initial header
    bool fin = (header[0] & 0x80) != 0;
    uint8_t rsv = (header[0] & 0x70) >> 4;
    ws_opcode opcode = static_cast<ws_opcode>(header[0] & 0x0F);
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;

    // Read extended payload length if needed
    if (payload_len == 126) {
        uint8_t len_bytes[2];
        read_exact(sock, len_bytes, 2);
        payload_len = (static_cast<uint64_t>(len_bytes[0]) << 8) |
                     static_cast<uint64_t>(len_bytes[1]);
    }
    else if (payload_len == 127) {
        uint8_t len_bytes[8];
        read_exact(sock, len_bytes, 8);
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | static_cast<uint64_t>(len_bytes[i]);
        }
    }

    // Read masking key if present
    uint8_t masking_key[4] = {0};
    if (masked) {
        read_exact(sock, masking_key, 4);
    }

    // Read payload
    std::vector<uint8_t> payload;
    if (payload_len > 0) {
        payload.resize(static_cast<size_t>(payload_len));
        read_exact(sock, &payload[0], static_cast<size_t>(payload_len));

        // Unmask if needed
        if (masked) {
            mask_payload(payload, masking_key);
        }
    }

    // Build frame
    r_websocket_frame frame;
    frame.fin = fin;
    frame.rsv1 = (rsv & 0x04) ? 1 : 0;
    frame.rsv2 = (rsv & 0x02) ? 1 : 0;
    frame.rsv3 = (rsv & 0x01) ? 1 : 0;
    frame.opcode = opcode;
    frame.masked = masked;
    frame.payload = payload;

    // Validate control frame constraints
    if (frame.is_control_frame()) {
        if (!frame.fin)
            R_THROW(("Control frames must not be fragmented"));
        if (frame.payload.size() > 125)
            R_THROW(("Control frame payload too large"));
    }

    return frame;
}

std::string r_websocket::compute_accept_key(const std::string& websocket_key)
{
    // Concatenate key with GUID
    std::string combined = websocket_key + WEBSOCKET_GUID;

    // Compute SHA-1 hash
    r_sha1 sha;
    sha.update(reinterpret_cast<const uint8_t*>(combined.c_str()), combined.length());
    sha.finalize();

    uint8_t hash[20];
    sha.get(hash);

    // Base64 encode
    return r_string_utils::to_base64(hash, 20);
}

void r_websocket::send_frame(r_utils::r_socket_base& sock, const r_websocket_frame& frame)
{
    auto frame_data = build_websocket_frame(frame);

    int sent = sock.send(&frame_data[0], frame_data.size());
    if (sent != static_cast<int>(frame_data.size()))
        R_THROW(("Failed to send complete WebSocket frame"));
}

void r_websocket::read_exact(r_utils::r_socket_base& sock, uint8_t* buffer, size_t length)
{
    size_t total_read = 0;
    while (total_read < length) {
        // Use r_networking::r_recv() which properly handles SSL WANT_READ/WANT_WRITE states
        uint64_t timeout_ms = 30000;  // 30 second timeout for WebSocket reads
        int bytes_read = r_networking::r_recv(sock, buffer + total_read, length - total_read, timeout_ms);

        if (bytes_read <= 0)
            R_THROW(("Socket closed or error while reading WebSocket frame"));

        total_read += bytes_read;
    }
}

std::string r_websocket::generate_websocket_key()
{
    // Generate 16 random bytes
    uint8_t random_bytes[16];
    for (int i = 0; i < 16; i++) {
        uint8_t key[4];
        generate_masking_key(key);
        random_bytes[i] = key[0];
    }

    // Base64 encode
    return r_string_utils::to_base64(random_bytes, 16);
}

void r_websocket::perform_client_handshake(
    r_utils::r_socket_base& sock,
    const std::string& host,
    const std::string& path)
{
    // Generate WebSocket key
    std::string ws_key = generate_websocket_key();

    // Build upgrade request
    std::string request =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + ws_key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    // Send request using r_networking::r_send for proper retry handling
    int sent = r_networking::r_send(sock, request.c_str(), request.length());
    if (sent != static_cast<int>(request.length()))
        R_THROW(("Failed to send WebSocket upgrade request"));

    // Read response
    std::string response;
    uint8_t buffer[1];
    bool headers_complete = false;

    while (!headers_complete) {
        // Use r_networking::r_recv with timeout for proper retry handling
        uint64_t timeout_ms = 5000;  // 5 second timeout for handshake
        int bytes_read = r_networking::r_recv(sock, buffer, 1, timeout_ms);
        if (bytes_read <= 0)
            R_THROW(("Socket closed while reading handshake response"));

        response += static_cast<char>(buffer[0]);

        // Check for end of headers
        if (response.length() >= 4 &&
            response.substr(response.length() - 4) == "\r\n\r\n") {
            headers_complete = true;
        }

        // Safety check - headers shouldn't be huge
        if (response.length() > 8192)
            R_THROW(("WebSocket handshake response too large"));
    }

    // Parse response
    // Check status line
    if (response.find("HTTP/1.1 101") != 0)
    {
        // Log the actual response for debugging
        std::string status_line = response.substr(0, response.find("\r\n"));
        std::string error_msg = "WebSocket upgrade failed - expected 101 status, got: " + status_line;
        R_THROW((error_msg.c_str()));
    }

    // Check Upgrade header
    if (response.find("Upgrade: websocket") == std::string::npos &&
        response.find("Upgrade: WebSocket") == std::string::npos)
        R_THROW(("WebSocket upgrade failed - missing Upgrade header"));

    // Check Connection header
    if (response.find("Connection: Upgrade") == std::string::npos &&
        response.find("Connection: upgrade") == std::string::npos)
        R_THROW(("WebSocket upgrade failed - missing Connection header"));

    // Verify Sec-WebSocket-Accept
    std::string expected_accept = compute_accept_key(ws_key);
    std::string accept_header = "Sec-WebSocket-Accept: " + expected_accept;

    if (response.find(accept_header) == std::string::npos)
        R_THROW(("WebSocket upgrade failed - invalid Sec-WebSocket-Accept"));

    // Handshake successful!
}
