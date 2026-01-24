
#include "test_r_websocket.h"
#include "r_http/r_websocket.h"
#include "r_http/r_websocket_frame.h"
#include "r_http/r_websocket_client.h"
#include "r_http/r_server_request.h"
#include "r_http/r_server_response.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <mutex>

using namespace std;
using namespace r_utils;
using namespace r_http;

REGISTER_TEST_FIXTURE(test_r_websocket);

void test_r_websocket::setup()
{
    r_raw_socket::socket_startup();
}

void test_r_websocket::teardown()
{
    r_raw_socket::socket_cleanup();
}

void test_r_websocket::test_frame_build_text()
{
    std::string message = "Hello, WebSocket!";
    std::vector<uint8_t> payload(message.begin(), message.end());

    auto frame_data = build_websocket_frame(ws_opcode::text, payload, true, false);

    // Verify frame structure
    RTF_ASSERT(frame_data.size() >= 2 + message.length());
    RTF_ASSERT((frame_data[0] & 0x80) != 0);  // FIN=1
    RTF_ASSERT((frame_data[0] & 0x0F) == 0x01);  // Opcode=text
    RTF_ASSERT((frame_data[1] & 0x80) == 0);  // MASK=0 (server)
    RTF_ASSERT((frame_data[1] & 0x7F) == message.length());  // Length
}

void test_r_websocket::test_frame_build_binary()
{
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};

    auto frame_data = build_websocket_frame(ws_opcode::binary, data, true, false);

    RTF_ASSERT(frame_data.size() >= 2 + data.size());
    RTF_ASSERT((frame_data[0] & 0x0F) == 0x02);  // Opcode=binary
    RTF_ASSERT((frame_data[1] & 0x7F) == data.size());
}

void test_r_websocket::test_frame_build_control()
{
    std::vector<uint8_t> empty;

    // Ping frame
    auto ping_data = build_websocket_frame(ws_opcode::ping, empty, true, false);
    RTF_ASSERT((ping_data[0] & 0x0F) == 0x09);  // Opcode=ping
    RTF_ASSERT((ping_data[1] & 0x7F) == 0);  // Empty payload

    // Pong frame
    auto pong_data = build_websocket_frame(ws_opcode::pong, empty, true, false);
    RTF_ASSERT((pong_data[0] & 0x0F) == 0x0A);  // Opcode=pong

    // Close frame
    auto close_data = build_websocket_frame(ws_opcode::close, empty, true, false);
    RTF_ASSERT((close_data[0] & 0x0F) == 0x08);  // Opcode=close
}

void test_r_websocket::test_frame_parse_text()
{
    std::string message = "Test message";
    std::vector<uint8_t> payload(message.begin(), message.end());

    // Build frame
    auto frame_data = build_websocket_frame(ws_opcode::text, payload, true, false);

    // Parse it back
    auto frame = parse_websocket_frame(frame_data);

    RTF_ASSERT(frame.fin == true);
    RTF_ASSERT(frame.opcode == ws_opcode::text);
    RTF_ASSERT(frame.masked == false);
    RTF_ASSERT(frame.payload == payload);
    RTF_ASSERT(frame.get_payload_as_string() == message);
}

void test_r_websocket::test_frame_parse_binary()
{
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};

    auto frame_data = build_websocket_frame(ws_opcode::binary, data, true, false);
    auto frame = parse_websocket_frame(frame_data);

    RTF_ASSERT(frame.opcode == ws_opcode::binary);
    RTF_ASSERT(frame.payload == data);
    RTF_ASSERT(frame.is_data_frame());
    RTF_ASSERT(!frame.is_control_frame());
}

void test_r_websocket::test_frame_parse_masked()
{
    std::string message = "Masked data";
    std::vector<uint8_t> payload(message.begin(), message.end());

    // Build masked frame (client->server)
    auto frame_data = build_websocket_frame(ws_opcode::text, payload, true, true);

    // Parse it back (should unmask automatically)
    auto frame = parse_websocket_frame(frame_data);

    RTF_ASSERT(frame.masked == true);
    RTF_ASSERT(frame.get_payload_as_string() == message);
}

void test_r_websocket::test_frame_build_extended_16bit()
{
    // Create payload > 125 bytes to trigger 16-bit length encoding
    std::vector<uint8_t> payload(200, 0xAB);

    auto frame_data = build_websocket_frame(ws_opcode::binary, payload, true, false);

    // Check for 16-bit extended length
    RTF_ASSERT((frame_data[1] & 0x7F) == 126);  // Length indicator = 126
    RTF_ASSERT(frame_data.size() >= 4 + payload.size());  // 2 byte header + 2 byte length + payload

    // Parse and verify
    auto frame = parse_websocket_frame(frame_data);
    RTF_ASSERT(frame.payload.size() == 200);
}

void test_r_websocket::test_frame_build_extended_64bit()
{
    // Create payload > 65535 bytes to trigger 64-bit length encoding
    std::vector<uint8_t> payload(70000, 0xCD);

    auto frame_data = build_websocket_frame(ws_opcode::binary, payload, true, false);

    // Check for 64-bit extended length
    RTF_ASSERT((frame_data[1] & 0x7F) == 127);  // Length indicator = 127
    RTF_ASSERT(frame_data.size() >= 10 + payload.size());  // 2 byte header + 8 byte length + payload

    // Parse and verify
    auto frame = parse_websocket_frame(frame_data);
    RTF_ASSERT(frame.payload.size() == 70000);
}

void test_r_websocket::test_compute_accept_key()
{
    // RFC 6455 example
    std::string client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    std::string expected_accept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

    std::string computed_accept = r_websocket::compute_accept_key(client_key);

    RTF_ASSERT(computed_accept == expected_accept);
}

void test_r_websocket::test_generate_websocket_key()
{
    std::string key1 = r_websocket::generate_websocket_key();
    std::string key2 = r_websocket::generate_websocket_key();

    // Key should be 24 characters (16 bytes base64 encoded)
    RTF_ASSERT(key1.length() == 24);
    RTF_ASSERT(key2.length() == 24);

    // Keys should be different (random)
    RTF_ASSERT(key1 != key2);
}

void test_r_websocket::test_client_server_text_exchange()
{
    int port = RTF_NEXT_PORT();

    // Server thread
    auto server_thread = std::thread([port]() {
        r_socket listen_sock;
        listen_sock.bind(port);
        listen_sock.listen();

        auto client_sock = listen_sock.accept();

        // Receive message from client
        auto frame = r_websocket::recv_frame(client_sock);
        RTF_ASSERT(frame.opcode == ws_opcode::text);
        RTF_ASSERT(frame.get_payload_as_string() == "Hello from client");

        // Send response (server doesn't mask)
        r_websocket::send_text(client_sock, "Hello from server", false);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Client thread
    r_socket client_sock;
    client_sock.connect("127.0.0.1", port);

    // Send message (client masks)
    r_websocket::send_text(client_sock, "Hello from client", true);

    // Receive response
    auto frame = r_websocket::recv_frame(client_sock);
    RTF_ASSERT(frame.opcode == ws_opcode::text);
    RTF_ASSERT(frame.get_payload_as_string() == "Hello from server");

    server_thread.join();
}

void test_r_websocket::test_client_server_binary_exchange()
{
    int port = RTF_NEXT_PORT();

    std::vector<uint8_t> client_data = {0x01, 0x02, 0x03, 0x04, 0x05};
    std::vector<uint8_t> server_data = {0xAA, 0xBB, 0xCC, 0xDD};

    auto server_thread = std::thread([port, server_data]() {
        r_socket listen_sock;
        listen_sock.bind(port);
        listen_sock.listen();

        auto client_sock = listen_sock.accept();

        // Receive binary from client
        auto frame = r_websocket::recv_frame(client_sock);
        RTF_ASSERT(frame.opcode == ws_opcode::binary);
        RTF_ASSERT(frame.payload.size() == 5);

        // Send binary response
        r_websocket::send_binary(client_sock, server_data, false);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    r_socket client_sock;
    client_sock.connect("127.0.0.1", port);

    // Send binary (masked)
    r_websocket::send_binary(client_sock, client_data, true);

    // Receive binary response
    auto frame = r_websocket::recv_frame(client_sock);
    RTF_ASSERT(frame.opcode == ws_opcode::binary);
    RTF_ASSERT(frame.payload == server_data);

    server_thread.join();
}

void test_r_websocket::test_client_server_ping_pong()
{
    int port = RTF_NEXT_PORT();

    std::vector<uint8_t> ping_payload = {0x01, 0x02, 0x03};

    auto server_thread = std::thread([port]() {
        r_socket listen_sock;
        listen_sock.bind(port);
        listen_sock.listen();

        auto client_sock = listen_sock.accept();

        // Receive ping from client
        auto ping_frame = r_websocket::recv_frame(client_sock);
        RTF_ASSERT(ping_frame.opcode == ws_opcode::ping);
        RTF_ASSERT(ping_frame.is_control_frame());

        // Send pong response (echo payload)
        r_websocket::send_pong(client_sock, ping_frame.payload, false);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    r_socket client_sock;
    client_sock.connect("127.0.0.1", port);

    // Send ping
    r_websocket::send_ping(client_sock, ping_payload, true);

    // Receive pong
    auto pong_frame = r_websocket::recv_frame(client_sock);
    RTF_ASSERT(pong_frame.opcode == ws_opcode::pong);
    RTF_ASSERT(pong_frame.payload == ping_payload);

    server_thread.join();
}

void test_r_websocket::test_client_server_close_handshake()
{
    int port = RTF_NEXT_PORT();

    auto server_thread = std::thread([port]() {
        r_socket listen_sock;
        listen_sock.bind(port);
        listen_sock.listen();

        auto client_sock = listen_sock.accept();

        // Receive close from client
        auto close_frame = r_websocket::recv_frame(client_sock);
        RTF_ASSERT(close_frame.opcode == ws_opcode::close);
        RTF_ASSERT(close_frame.is_control_frame());

        // Parse close code (first 2 bytes, big-endian)
        if (close_frame.payload.size() >= 2) {
            uint16_t code = (static_cast<uint16_t>(close_frame.payload[0]) << 8) |
                           static_cast<uint16_t>(close_frame.payload[1]);
            RTF_ASSERT(code == static_cast<uint16_t>(ws_close_code::normal));
        }

        // Send close response
        r_websocket::send_close(client_sock, ws_close_code::normal, "", false);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    r_socket client_sock;
    client_sock.connect("127.0.0.1", port);

    // Send close
    r_websocket::send_close(client_sock, ws_close_code::normal, "Test close", true);

    // Receive close response
    auto close_frame = r_websocket::recv_frame(client_sock);
    RTF_ASSERT(close_frame.opcode == ws_opcode::close);

    server_thread.join();
}

// Test full HTTP upgrade handshake using r_http server classes
void test_r_websocket::test_full_http_upgrade_handshake() {
    int port = RTF_NEXT_PORT();

    // Server thread: Accept connection, handle HTTP upgrade, then exchange WebSocket messages
    std::thread server_thread([port]() {
        r_socket server_sock;
        server_sock.bind(port, "0.0.0.0");
        server_sock.listen();
        r_socket client_conn = server_sock.accept();

        // Read HTTP upgrade request using r_server_request
        r_server_request request;
        request.read_request(client_conn);

        // Verify it's a WebSocket upgrade request
        RTF_ASSERT(request.is_get_request());

        auto upgrade_header = request.get_header("Upgrade");
        RTF_ASSERT(!upgrade_header.is_null());
        RTF_ASSERT(r_string_utils::to_lower(upgrade_header.value()) == "websocket");

        auto connection_header = request.get_header("Connection");
        RTF_ASSERT(!connection_header.is_null());
        RTF_ASSERT(r_string_utils::to_lower(connection_header.value()) == "upgrade");

        auto ws_key_header = request.get_header("Sec-WebSocket-Key");
        RTF_ASSERT(!ws_key_header.is_null());

        auto ws_version_header = request.get_header("Sec-WebSocket-Version");
        RTF_ASSERT(!ws_version_header.is_null());
        RTF_ASSERT(ws_version_header.value() == "13");

        // Compute accept key
        std::string ws_key = ws_key_header.value();
        std::string accept_key = r_websocket::compute_accept_key(ws_key);

        // Send HTTP 101 Switching Protocols response
        r_server_response response(response_switching_protocols);
        response.add_additional_header("Upgrade", "websocket");
        response.add_additional_header("Connection", "Upgrade");
        response.add_additional_header("Sec-WebSocket-Accept", accept_key);
        response.write_response(client_conn);

        // Now we're in WebSocket mode - receive a message from client
        auto frame = r_websocket::recv_frame(client_conn);
        RTF_ASSERT(frame.opcode == ws_opcode::text);
        RTF_ASSERT(frame.fin);
        RTF_ASSERT(frame.masked);  // Client messages must be masked
        RTF_ASSERT(frame.get_payload_as_string() == "Hello from client!");

        // Send a response back (unmasked, since we're the server)
        r_websocket::send_text(client_conn, "Hello from server!", false);

        // Receive close frame
        auto close_frame = r_websocket::recv_frame(client_conn);
        RTF_ASSERT(close_frame.opcode == ws_opcode::close);

        // Send close response
        r_websocket::send_close(client_conn, ws_close_code::normal, "", false);
    });

    rtf_usleep(100000);  // Give server time to start

    // Client: Connect and perform WebSocket upgrade handshake
    r_socket client_sock;
    client_sock.connect("127.0.0.1", port);

    // Perform HTTP upgrade handshake
    r_websocket::perform_client_handshake(client_sock, "localhost", "/ws");

    // Now we're in WebSocket mode - send a message (masked, since we're the client)
    r_websocket::send_text(client_sock, "Hello from client!", true);

    // Receive server response
    auto response_frame = r_websocket::recv_frame(client_sock);
    RTF_ASSERT(response_frame.opcode == ws_opcode::text);
    RTF_ASSERT(response_frame.fin);
    RTF_ASSERT(!response_frame.masked);  // Server messages must NOT be masked
    RTF_ASSERT(response_frame.get_payload_as_string() == "Hello from server!");

    // Close connection
    r_websocket::send_close(client_sock, ws_close_code::normal, "", true);

    // Receive close response
    auto close_frame = r_websocket::recv_frame(client_sock);
    RTF_ASSERT(close_frame.opcode == ws_opcode::close);

    server_thread.join();
}

// Test r_websocket_client wrapper with callbacks and background threads
void test_r_websocket::test_websocket_client_wrapper() {
    int port = RTF_NEXT_PORT();

    // Track received messages with thread-safe synchronization
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::string> received_messages;
    std::atomic<bool> got_server_hello{false};
    std::atomic<bool> got_server_response{false};

    // Server thread: Handle HTTP upgrade and exchange messages
    std::thread server_thread([port, &mtx, &cv, &got_server_hello, &got_server_response]() {
        r_socket server_sock;
        server_sock.bind(port, "0.0.0.0");
        server_sock.listen();
        r_socket client_conn = server_sock.accept();

        // Read HTTP upgrade request
        r_server_request request;
        request.read_request(client_conn);

        // Send HTTP 101 Switching Protocols response
        auto ws_key = request.get_header("Sec-WebSocket-Key").value();
        std::string accept_key = r_websocket::compute_accept_key(ws_key);

        r_server_response response(response_switching_protocols);
        response.add_additional_header("Upgrade", "websocket");
        response.add_additional_header("Connection", "Upgrade");
        response.add_additional_header("Sec-WebSocket-Accept", accept_key);
        response.write_response(client_conn);

        // Receive first message from client
        auto frame1 = r_websocket::recv_frame(client_conn);
        RTF_ASSERT(frame1.opcode == ws_opcode::text);
        RTF_ASSERT(frame1.get_payload_as_string() == "Hello from client!");
        got_server_hello = true;

        // Send response to client
        r_websocket::send_text(client_conn, "Hello from server!", false);

        // Receive second message from client
        auto frame2 = r_websocket::recv_frame(client_conn);
        RTF_ASSERT(frame2.opcode == ws_opcode::text);
        RTF_ASSERT(frame2.get_payload_as_string() == "How are you?");
        got_server_response = true;

        // Send another response
        r_websocket::send_text(client_conn, "I'm doing great!", false);

        // Wait for close from client
        auto close_frame_srv = r_websocket::recv_frame(client_conn);
        RTF_ASSERT(close_frame_srv.opcode == ws_opcode::close);

        // Send close response
        r_websocket::send_close(client_conn, ws_close_code::normal, "", false);
    });

    rtf_usleep(100000);  // Give server time to start

    // Create r_websocket_client with a connected and upgraded socket
    auto socket = std::make_unique<r_socket>();
    socket->connect("127.0.0.1", port);

    // Create client wrapper
    r_websocket_client client(std::move(socket));
    client.handshake("localhost", "/ws");

    // Set up message callback
    client.set_message_callback([&mtx, &cv, &received_messages](const r_websocket_frame& frame) {
        if (frame.opcode == ws_opcode::text) {
            std::lock_guard<std::mutex> lock(mtx);
            received_messages.push_back(frame.get_payload_as_string());
            cv.notify_one();
        }
    });

    // Start background threads (receive loop and keepalive)
    client.start();

    // Send first message using client wrapper
    client.send_text("Hello from client!");

    // Wait for first response
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, std::chrono::seconds(2), [&received_messages]() {
            return received_messages.size() >= 1;
        });
        RTF_ASSERT(received_messages.size() >= 1);
        RTF_ASSERT(received_messages[0] == "Hello from server!");
    }

    // Send second message
    client.send_text("How are you?");

    // Wait for second response
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, std::chrono::seconds(2), [&received_messages]() {
            return received_messages.size() >= 2;
        });
        RTF_ASSERT(received_messages.size() >= 2);
        RTF_ASSERT(received_messages[1] == "I'm doing great!");
    }

    // Verify server received our messages
    RTF_ASSERT(got_server_hello.load());
    RTF_ASSERT(got_server_response.load());

    // Close connection
    client.close(ws_close_code::normal, "Test complete");

    // Stop background threads
    client.stop();

    server_thread.join();
}
