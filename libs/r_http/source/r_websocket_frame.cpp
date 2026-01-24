#include "r_http/r_websocket_frame.h"
#include "r_utils/r_exception.h"
#include <cstring>
#include <random>
#include <sstream>

using namespace r_http;
using namespace r_utils;

// Constructor
r_websocket_frame::r_websocket_frame()
    : fin(true)
    , rsv1(0)
    , rsv2(0)
    , rsv3(0)
    , opcode(ws_opcode::text)
    , masked(false)
    , payload()
{
}

r_websocket_frame::r_websocket_frame(ws_opcode op, const std::vector<uint8_t>& data, bool fin_flag)
    : fin(fin_flag)
    , rsv1(0)
    , rsv2(0)
    , rsv3(0)
    , opcode(op)
    , masked(false)
    , payload(data)
{
}

bool r_websocket_frame::is_control_frame() const
{
    return (static_cast<uint8_t>(opcode) & 0x08) != 0;
}

bool r_websocket_frame::is_data_frame() const
{
    return !is_control_frame();
}

std::string r_websocket_frame::get_payload_as_string() const
{
    return std::string(payload.begin(), payload.end());
}

// Parse a WebSocket frame from raw data
r_websocket_frame r_http::parse_websocket_frame(const std::vector<uint8_t>& data)
{
    if (data.size() < 2)
        R_THROW(("WebSocket frame too short (need at least 2 bytes)"));

    r_websocket_frame frame;
    size_t offset = 0;

    // Byte 0: FIN, RSV, Opcode
    uint8_t byte0 = data[offset++];
    frame.fin = (byte0 & 0x80) != 0;
    frame.rsv1 = (byte0 & 0x40) != 0 ? 1 : 0;
    frame.rsv2 = (byte0 & 0x20) != 0 ? 1 : 0;
    frame.rsv3 = (byte0 & 0x10) != 0 ? 1 : 0;
    frame.opcode = static_cast<ws_opcode>(byte0 & 0x0F);

    // Validate opcode
    uint8_t opcode_val = static_cast<uint8_t>(frame.opcode);
    if ((opcode_val >= 3 && opcode_val <= 7) || (opcode_val >= 0xB && opcode_val <= 0xF))
        R_THROW(("Invalid WebSocket opcode: %d", opcode_val));

    // Byte 1: MASK, Payload length
    uint8_t byte1 = data[offset++];
    frame.masked = (byte1 & 0x80) != 0;
    uint64_t payload_length = byte1 & 0x7F;

    // Extended payload length
    if (payload_length == 126) {
        if (data.size() < offset + 2)
            R_THROW(("WebSocket frame too short for 16-bit length"));

        payload_length = (static_cast<uint64_t>(data[offset]) << 8) |
                        static_cast<uint64_t>(data[offset + 1]);
        offset += 2;
    }
    else if (payload_length == 127) {
        if (data.size() < offset + 8)
            R_THROW(("WebSocket frame too short for 64-bit length"));

        payload_length = 0;
        for (int i = 0; i < 8; i++) {
            payload_length = (payload_length << 8) | static_cast<uint64_t>(data[offset + i]);
        }
        offset += 8;
    }

    // Validate control frame constraints
    if (frame.is_control_frame()) {
        if (!frame.fin)
            R_THROW(("Control frames must not be fragmented"));
        if (payload_length > 125)
            R_THROW(("Control frame payload too large: %llu bytes (max 125)",
                   static_cast<unsigned long long>(payload_length)));
    }

    // Masking key (if present)
    uint8_t masking_key[4] = {0};
    if (frame.masked) {
        if (data.size() < offset + 4)
            R_THROW(("WebSocket frame too short for masking key"));

        memcpy(masking_key, &data[offset], 4);
        offset += 4;
    }

    // Payload data
    if (data.size() < offset + payload_length)
        R_THROW(("WebSocket frame too short for payload (expected %llu bytes)",
               static_cast<unsigned long long>(payload_length)));

    frame.payload.resize(static_cast<size_t>(payload_length));
    if (payload_length > 0) {
        memcpy(&frame.payload[0], &data[offset], static_cast<size_t>(payload_length));

        // Unmask if needed
        if (frame.masked) {
            mask_payload(frame.payload, masking_key);
        }
    }

    return frame;
}

// Build a WebSocket frame for sending
std::vector<uint8_t> r_http::build_websocket_frame(
    ws_opcode opcode,
    const std::vector<uint8_t>& payload,
    bool fin,
    bool mask_frame)
{
    r_websocket_frame frame;
    frame.fin = fin;
    frame.opcode = opcode;
    frame.masked = mask_frame;
    frame.payload = payload;

    return build_websocket_frame(frame);
}

std::vector<uint8_t> r_http::build_websocket_frame(const r_websocket_frame& frame)
{
    std::vector<uint8_t> result;

    // Validate control frame constraints
    if (frame.is_control_frame()) {
        if (!frame.fin)
            R_THROW(("Control frames must have FIN=1"));
        if (frame.payload.size() > 125)
            R_THROW(("Control frame payload too large: %zu bytes (max 125)", frame.payload.size()));
    }

    // Byte 0: FIN, RSV, Opcode
    uint8_t byte0 = (frame.fin ? 0x80 : 0x00) |
                    (frame.rsv1 ? 0x40 : 0x00) |
                    (frame.rsv2 ? 0x20 : 0x00) |
                    (frame.rsv3 ? 0x10 : 0x00) |
                    static_cast<uint8_t>(frame.opcode);
    result.push_back(byte0);

    // Byte 1: MASK, Payload length
    uint64_t payload_len = frame.payload.size();
    uint8_t byte1 = frame.masked ? 0x80 : 0x00;

    if (payload_len <= 125) {
        byte1 |= static_cast<uint8_t>(payload_len);
        result.push_back(byte1);
    }
    else if (payload_len <= 0xFFFF) {
        byte1 |= 126;
        result.push_back(byte1);
        result.push_back(static_cast<uint8_t>((payload_len >> 8) & 0xFF));
        result.push_back(static_cast<uint8_t>(payload_len & 0xFF));
    }
    else {
        byte1 |= 127;
        result.push_back(byte1);
        for (int i = 7; i >= 0; i--) {
            result.push_back(static_cast<uint8_t>((payload_len >> (i * 8)) & 0xFF));
        }
    }

    // Masking key (if needed)
    uint8_t masking_key[4] = {0};
    if (frame.masked) {
        generate_masking_key(masking_key);
        result.insert(result.end(), masking_key, masking_key + 4);
    }

    // Payload data
    if (!frame.payload.empty()) {
        std::vector<uint8_t> payload_copy = frame.payload;

        // Mask if needed
        if (frame.masked) {
            mask_payload(payload_copy, masking_key);
        }

        result.insert(result.end(), payload_copy.begin(), payload_copy.end());
    }

    return result;
}

// Mask/unmask payload (XOR operation - same for both directions)
void r_http::mask_payload(std::vector<uint8_t>& payload, const uint8_t masking_key[4])
{
    for (size_t i = 0; i < payload.size(); i++) {
        payload[i] ^= masking_key[i % 4];
    }
}

// Generate random masking key
void r_http::generate_masking_key(uint8_t masking_key[4])
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 255);

    for (int i = 0; i < 4; i++) {
        masking_key[i] = static_cast<uint8_t>(dis(gen));
    }
}
