# WebSocket Usage Guide

## Overview

The `r_websocket` implementation provides RFC 6455 compliant WebSocket protocol support for the r_http library. It operates as a stateless protocol layer on top of existing sockets.

## Basic Usage

### 1. Detecting WebSocket Upgrade Requests

WebSocket connections start as HTTP requests with specific headers:

```cpp
#include "r_http/r_websocket.h"

bool is_websocket_upgrade_request(const r_server_request& request)
{
    // Check required headers
    auto upgrade = request.get_header("Upgrade");
    auto connection = request.get_header("Connection");
    auto ws_version = request.get_header("Sec-WebSocket-Version");
    auto ws_key = request.get_header("Sec-WebSocket-Key");

    // Case-insensitive comparison
    return !upgrade.empty() &&
           !connection.empty() &&
           ws_version == "13" &&
           !ws_key.empty() &&
           (upgrade == "websocket" || upgrade == "WebSocket");
}
```

### 2. Performing the Handshake

When a WebSocket upgrade is detected, send the 101 response:

```cpp
void handle_websocket_upgrade(
    r_utils::r_buffered_socket<r_utils::r_ssl_socket>& conn,
    const r_server_request& request)
{
    // Get the WebSocket key from request
    std::string ws_key = request.get_header("Sec-WebSocket-Key");

    // Compute accept key
    std::string accept_key = r_websocket::compute_accept_key(ws_key);

    // Send 101 Switching Protocols response
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept_key + "\r\n"
        "\r\n";

    conn.send(response.c_str(), response.length());
    conn.flush();
}
```

### 3. WebSocket Communication

After the handshake, use the WebSocket protocol functions:

```cpp
void websocket_echo_handler(r_utils::r_socket_base& sock)
{
    try {
        while (true) {
            // Receive frame
            auto frame = r_websocket::recv_frame(sock);

            // Handle different frame types
            if (frame.opcode == ws_opcode::text) {
                // Echo text back
                std::string msg = frame.get_payload_as_string();
                r_websocket::send_text(sock, "Echo: " + msg);
            }
            else if (frame.opcode == ws_opcode::binary) {
                // Echo binary back
                r_websocket::send_binary(sock, frame.payload);
            }
            else if (frame.opcode == ws_opcode::ping) {
                // Respond with pong
                r_websocket::send_pong(sock, frame.payload);
            }
            else if (frame.opcode == ws_opcode::close) {
                // Client wants to close
                r_websocket::send_close(sock);
                break;
            }
        }
    }
    catch (const r_exception& ex) {
        // Socket error or protocol violation
        R_LOG_EXCEPTION(ex);
    }
}
```

### 4. Complete Integration Example

```cpp
#include "r_http/r_web_server.h"
#include "r_http/r_websocket.h"

class my_web_server
{
public:
    void setup()
    {
        // Regular HTTP route
        _server.add_route(
            GET,
            "/api/status",
            [](const auto& ws, auto& conn, const auto& req) {
                r_server_response resp;
                resp.set_body("{\"status\":\"ok\"}");
                return resp;
            }
        );

        // WebSocket upgrade route
        _server.add_route(
            GET,
            "/ws/stream",
            [this](const auto& ws, auto& conn, const auto& req) {
                if (is_websocket_upgrade_request(req)) {
                    handle_websocket_upgrade(conn, req);

                    // Get underlying socket for WebSocket communication
                    auto& sock = conn.get_underlying_socket();
                    websocket_echo_handler(sock);

                    // Return empty response (connection already handled)
                    return r_server_response();
                }
                else {
                    // Not a WebSocket upgrade
                    r_server_response resp;
                    resp.set_status_code(r_status_code::Code::BadRequest);
                    resp.set_body("Expected WebSocket upgrade");
                    return resp;
                }
            }
        );
    }

private:
    r_web_server<r_ssl_socket> _server{8888};
};
```

## API Reference

### Frame Types (ws_opcode)

- `ws_opcode::text` - UTF-8 text message
- `ws_opcode::binary` - Binary data message
- `ws_opcode::close` - Connection close
- `ws_opcode::ping` - Keepalive ping
- `ws_opcode::pong` - Ping response

### Sending Messages

```cpp
// Send text
r_websocket::send_text(sock, "Hello, world!");

// Send binary
std::vector<uint8_t> data = {...};
r_websocket::send_binary(sock, data);

// Send ping
r_websocket::send_ping(sock);

// Send pong (in response to ping)
r_websocket::send_pong(sock, frame.payload);

// Send close
r_websocket::send_close(sock, ws_close_code::normal, "Goodbye");
```

### Receiving Frames

```cpp
r_websocket_frame frame = r_websocket::recv_frame(sock);

if (frame.opcode == ws_opcode::text) {
    std::string msg = frame.get_payload_as_string();
    // Handle text message
}
```

### Close Codes (ws_close_code)

- `normal` (1000) - Normal closure
- `going_away` (1001) - Endpoint going away
- `protocol_error` (1002) - Protocol error
- `unsupported_data` (1003) - Unsupported data type
- `invalid_frame_payload` (1007) - Invalid payload
- `policy_violation` (1008) - Policy violation
- `message_too_big` (1009) - Message too large
- `internal_error` (1011) - Internal server error

## Video Streaming Example

For your surveillance application, here's how to stream video:

```cpp
void stream_video_websocket(r_utils::r_socket_base& sock)
{
    try {
        while (true) {
            // Check for control messages
            auto frame = r_websocket::recv_frame(sock);

            if (frame.opcode == ws_opcode::text) {
                // Parse command (e.g., {"cmd":"start_stream"})
                std::string cmd = frame.get_payload_as_string();

                if (cmd == "start") {
                    // Start sending video frames
                    while (true) {
                        std::vector<uint8_t> jpeg_frame = get_next_frame();
                        r_websocket::send_binary(sock, jpeg_frame);

                        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30fps
                    }
                }
            }
            else if (frame.opcode == ws_opcode::close) {
                r_websocket::send_close(sock);
                break;
            }
        }
    }
    catch (const r_exception& ex) {
        R_LOG_EXCEPTION(ex);
    }
}
```

## Client-Side JavaScript Example

```javascript
// Connect to WebSocket
const ws = new WebSocket('wss://localhost:8888/ws/stream');

ws.onopen = () => {
    console.log('Connected');
    ws.send('start');  // Request video stream
};

ws.onmessage = (event) => {
    if (event.data instanceof Blob) {
        // Binary frame (video frame)
        const url = URL.createObjectURL(event.data);
        document.getElementById('video').src = url;
    } else {
        // Text frame (control message)
        console.log('Message:', event.data);
    }
};

ws.onclose = () => {
    console.log('Disconnected');
};
```

## Notes

- **No fragmentation support**: All messages are sent as single frames (FIN=1)
- **No compression**: permessage-deflate extension not implemented
- **Server frames are unmasked**: As per RFC 6455, server→client frames have MASK=0
- **Client frames are masked**: Client→server frames must have MASK=1 (enforced by browsers)
- **Control frame limits**: Ping/pong/close payloads must be ≤125 bytes

## Error Handling

All WebSocket functions throw `r_exception` on errors:
- Socket I/O errors
- Protocol violations
- Invalid frames

Wrap WebSocket operations in try/catch blocks.
