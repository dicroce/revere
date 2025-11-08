# WebSocket Client Usage Guide

## Overview

The `r_websocket_client` provides a complete client-side WebSocket implementation for connecting to external WebSocket servers (e.g., cloud APIs). It handles:

- TLS/SSL connections via r_ssl_socket
- WebSocket handshake
- Automatic ping/pong keepalive
- Message framing with proper masking (RFC 6455 requirement)
- Graceful connection closure

## Basic Client Example

```cpp
#include "r_http/r_websocket_client.h"
#include "r_utils/r_ssl_socket.h"

// Connect to WebSocket server
auto socket = std::make_unique<r_ssl_socket>();
socket->connect("api.example.com", 443);

// Create WebSocket client
r_websocket_client client(std::move(socket));

// Perform handshake
client.handshake("api.example.com", "/ws/v1");

// Set message handler
client.set_message_callback([](const r_websocket_frame& frame) {
    if (frame.opcode == ws_opcode::text) {
        std::string msg = frame.get_payload_as_string();
        std::cout << "Received: " << msg << std::endl;
    }
    else if (frame.opcode == ws_opcode::binary) {
        std::cout << "Received binary: " << frame.payload.size() << " bytes" << std::endl;
    }
});

// Start background threads
client.start();

// Send messages
client.send_text("{\"action\":\"subscribe\",\"channel\":\"events\"}");

// Keep running
while (client.is_connected()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

// Clean up (automatic in destructor)
client.close();
```

## Connecting to Cloud API (with TLS)

```cpp
class cloud_websocket_client {
public:
    void connect(const std::string& api_url, int port = 443) {
        // Create SSL socket
        auto socket = std::make_unique<r_ssl_socket>();

        try {
            // Connect to cloud API
            socket->connect(api_url, port);

            // Create WebSocket client
            _client = std::make_unique<r_websocket_client>(std::move(socket));

            // Perform handshake
            _client->handshake(api_url, "/ws");

            // Set up message handler
            _client->set_message_callback(
                std::bind(&cloud_websocket_client::on_message, this, std::placeholders::_1)
            );

            // Start
            _client->start();

            R_LOG_INFO("Connected to cloud API: %s", api_url.c_str());
        }
        catch (const r_exception& ex) {
            R_LOG_EXCEPTION(ex);
            throw;
        }
    }

    void send_event(const std::string& event_type, const std::string& data) {
        if (!_client || !_client->is_connected()) {
            R_LOG_WARNING("Not connected to cloud API");
            return;
        }

        // Build JSON message
        std::string msg = "{\"type\":\"" + event_type + "\",\"data\":\"" + data + "\"}";

        try {
            _client->send_text(msg);
        }
        catch (const r_exception& ex) {
            R_LOG_EXCEPTION(ex);
        }
    }

    void disconnect() {
        if (_client) {
            _client->close(ws_close_code::normal, "Client disconnect");
        }
    }

private:
    void on_message(const r_websocket_frame& frame) {
        if (frame.opcode == ws_opcode::text) {
            std::string msg = frame.get_payload_as_string();
            R_LOG_INFO("Cloud message: %s", msg.c_str());

            // Parse and handle message
            handle_cloud_message(msg);
        }
    }

    void handle_cloud_message(const std::string& msg) {
        // Parse JSON and handle different message types
        // ...
    }

    std::unique_ptr<r_websocket_client> _client;
};
```

## Surveillance Application Example

Uploading motion events to cloud via WebSocket:

```cpp
class surveillance_cloud_uploader {
public:
    void connect_to_cloud() {
        auto socket = std::make_unique<r_ssl_socket>();
        socket->connect("surveillance.cloud.com", 443);

        _ws_client = std::make_unique<r_websocket_client>(std::move(socket));
        _ws_client->handshake("surveillance.cloud.com", "/api/v1/cameras");

        // Authenticate
        _ws_client->set_message_callback([this](const r_websocket_frame& frame) {
            if (frame.opcode == ws_opcode::text) {
                handle_cloud_response(frame.get_payload_as_string());
            }
        });

        _ws_client->start();

        // Send auth
        std::string auth_msg = "{\"type\":\"auth\",\"token\":\"" + _api_token + "\"}";
        _ws_client->send_text(auth_msg);
    }

    void upload_motion_event(const motion_event& event) {
        if (!_ws_client || !_ws_client->is_connected()) {
            R_LOG_WARNING("Not connected to cloud");
            return;
        }

        // Send motion event as JSON
        std::string msg =
            "{"
            "\"type\":\"motion_detected\","
            "\"camera_id\":\"" + event.camera_id + "\","
            "\"timestamp\":" + std::to_string(event.timestamp) + ","
            "\"confidence\":" + std::to_string(event.confidence) +
            "}";

        _ws_client->send_text(msg);
    }

    void upload_snapshot(const std::string& camera_id, const std::vector<uint8_t>& jpeg_data) {
        if (!_ws_client || !_ws_client->is_connected()) {
            return;
        }

        // Send binary snapshot
        // Note: You may want to add a text frame first with metadata
        std::string metadata = "{\"type\":\"snapshot\",\"camera_id\":\"" + camera_id + "\"}";
        _ws_client->send_text(metadata);

        // Then send the JPEG
        _ws_client->send_binary(jpeg_data);
    }

private:
    void handle_cloud_response(const std::string& msg) {
        // Handle ACKs, commands from cloud, etc.
        if (msg.find("\"status\":\"authenticated\"") != std::string::npos) {
            R_LOG_INFO("Authenticated with cloud");
            _authenticated = true;
        }
    }

    std::unique_ptr<r_websocket_client> _ws_client;
    std::string _api_token;
    bool _authenticated{false};
};
```

## Non-TLS Connection (Plain WebSocket)

For testing or non-secure connections:

```cpp
#include "r_utils/r_socket.h"

// Use regular socket instead of SSL
auto socket = std::make_unique<r_socket>();
socket->connect("localhost", 8080);

r_websocket_client client(std::move(socket));
client.handshake("localhost", "/ws");
client.start();
```

## Error Handling

```cpp
try {
    auto socket = std::make_unique<r_ssl_socket>();
    socket->connect("api.example.com", 443);

    r_websocket_client client(std::move(socket));
    client.handshake("api.example.com", "/ws");

    client.set_message_callback([](const r_websocket_frame& frame) {
        // Handle messages
    });

    client.start();

    // Send messages
    while (client.is_connected()) {
        client.send_text("ping");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
catch (const r_exception& ex) {
    R_LOG_EXCEPTION(ex);
    // Handle connection failure
}
```

## Connection Lifecycle

```
1. Create socket (r_socket or r_ssl_socket)
2. Connect socket to remote host:port
3. Create r_websocket_client with socket
4. Perform handshake
5. Set message callback
6. Start background threads
7. Send/receive messages
8. Close connection (automatic in destructor)
```

## Thread Safety

- **send_text()** / **send_binary()**: Thread-safe (mutex protected)
- **Message callback**: Called from receive thread
- **Automatic ping/pong**: Handled by background keepalive thread
- **Graceful shutdown**: Handled by destructor

## Keepalive Behavior

- Pings sent every 30 seconds automatically
- Server pings responded to with pongs automatically
- Connection marked dead if send fails

## Key Differences from Server

| Feature | Server | Client |
|---------|--------|--------|
| Frame masking | No (MASK=0) | Yes (MASK=1) |
| Handshake | Receive upgrade, send 101 | Send upgrade, validate 101 |
| Default port | Server-defined | 80 (ws://) or 443 (wss://) |
| Socket ownership | Server manages | Client owns |

## Advanced: Custom Protocols

```cpp
// Example: Implementing custom protocol over WebSocket
class custom_protocol_client {
public:
    void connect() {
        auto socket = std::make_unique<r_ssl_socket>();
        socket->connect("api.example.com", 443);

        _ws = std::make_unique<r_websocket_client>(std::move(socket));
        _ws->handshake("api.example.com", "/ws");
        _ws->set_message_callback([this](const r_websocket_frame& frame) {
            handle_protocol_message(frame);
        });
        _ws->start();
    }

    void send_command(uint8_t cmd_id, const std::vector<uint8_t>& payload) {
        // Custom binary protocol: [cmd_id][payload]
        std::vector<uint8_t> msg;
        msg.push_back(cmd_id);
        msg.insert(msg.end(), payload.begin(), payload.end());

        _ws->send_binary(msg);
    }

private:
    void handle_protocol_message(const r_websocket_frame& frame) {
        if (frame.opcode != ws_opcode::binary || frame.payload.empty()) {
            return;
        }

        uint8_t cmd_id = frame.payload[0];
        std::vector<uint8_t> payload(frame.payload.begin() + 1, frame.payload.end());

        // Dispatch based on command
        switch (cmd_id) {
            case 0x01: handle_status_update(payload); break;
            case 0x02: handle_data_response(payload); break;
            // ...
        }
    }

    std::unique_ptr<r_websocket_client> _ws;
};
```

## Testing Connection

```cpp
// Simple test program
int main() {
    try {
        R_LOG_INFO("Connecting to WebSocket server...");

        auto socket = std::make_unique<r_ssl_socket>();
        socket->connect("echo.websocket.org", 443);

        r_websocket_client client(std::move(socket));
        client.handshake("echo.websocket.org", "/");

        client.set_message_callback([](const r_websocket_frame& frame) {
            if (frame.opcode == ws_opcode::text) {
                R_LOG_INFO("Echo: %s", frame.get_payload_as_string().c_str());
            }
        });

        client.start();

        // Send test messages
        for (int i = 0; i < 5; i++) {
            std::string msg = "Test message " + std::to_string(i);
            client.send_text(msg);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        client.close();
        R_LOG_INFO("Test complete");
    }
    catch (const r_exception& ex) {
        R_LOG_EXCEPTION(ex);
        return 1;
    }

    return 0;
}
```

## Notes

- All client frames are automatically masked (RFC 6455 requirement)
- Connection state managed automatically
- Background threads handle ping/pong
- Destructor ensures clean shutdown
- Works with both TLS (wss://) and plain (ws://) connections
