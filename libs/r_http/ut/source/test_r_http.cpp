
#include "test_r_http.h"
#include "r_utils/r_socket.h"
#include "r_http/r_client_request.h"
#include "r_http/r_server_request.h"
#include "r_http/r_client_response.h"
#include "r_http/r_server_response.h"

#include <chrono>
#include <thread>
#include <climits>
#include <numeric>

using namespace std;
using namespace std::chrono;
using namespace r_utils;
using namespace r_http;

REGISTER_TEST_FIXTURE(test_r_http);

void test_r_http::setup()
{
    r_raw_socket::socket_startup();
}

void test_r_http::teardown()
{
    r_raw_socket::socket_cleanup();
}

void test_r_http::test_client_request_default_ctor()
{
    int port = RTF_NEXT_PORT();
    r_client_request request("127.0.0.1", port);
}

void test_r_http::test_client_request_copy_ctor()
{
    int port = RTF_NEXT_PORT();
    r_client_request ra("127.0.0.1", port);
    ra.set_uri("/vmas/device/status/?a=1");
    r_client_request rb(ra);
    RTF_ASSERT(rb._uri.get_full_raw_uri() == "/vmas/device/status?a=1");
}

void test_r_http::test_client_request_assignment_op()
{
    int port = RTF_NEXT_PORT();
    r_client_request ra("127.0.0.1", port);
    ra.set_uri("/vmas/device/status/");
    r_client_request rb("127.0.0.1", port);
    rb = ra;
    RTF_ASSERT(rb._uri.get_full_raw_uri() == "/vmas/device/status");
}

void test_r_http::test_client_request_write_request()
{
    int port = RTF_NEXT_PORT();
    std::exception_ptr serverException;

    auto th = std::thread([port, &serverException](){
        try {
            r_socket socket;
            socket.bind(port);
            socket.listen();

            auto clientSocket = socket.accept();

            r_server_request request;

            request.read_request(clientSocket);

            RTF_ASSERT(request.get_method() == METHOD_GET);

            r_uri u = request.get_uri();

            RTF_ASSERT(u.get_full_raw_uri() == "/vmas/device/status");
        } catch(...) {
            serverException = std::current_exception();
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));

    r_socket socket;
    socket.connect("127.0.0.1", port);

    r_client_request request("127.0.0.1", port);

    request.set_uri("/vmas/device/status");

    request.write_request(socket);

    th.join();

    if(serverException)
        std::rethrow_exception(serverException);
}

void test_r_http::test_client_response_default_constructor()
{
    r_client_response response;
}

void test_r_http::test_client_response_receive()
{
    int port = RTF_NEXT_PORT();
    std::exception_ptr serverException;

    auto th = thread([&](){
        try {
            r_socket socket;
            socket.bind(port);
            socket.listen();

            auto clientSocket = socket.accept();

            r_server_request ssRequest;
            ssRequest.read_request(clientSocket);

            auto body = ssRequest.get_body_as_string();

            r_server_response ssResponse;
            ssResponse.set_body(body);
            RTF_ASSERT_NO_THROW(ssResponse.write_response(clientSocket));

            clientSocket.close();

            socket.close();
        } catch(...) {
            serverException = std::current_exception();
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));

    r_socket socket;
    socket.connect("127.0.0.1", port);

    string message = "Hello, Webby!";

    r_client_request request("127.0.0.1", port);

    request.set_method(METHOD_POST);
    request.set_body((uint8_t*)message.c_str(), message.length());

    RTF_ASSERT_NO_THROW(request.write_request(socket));

    r_client_response response;
    response.read_response(socket);

    auto responseBody = response.get_body_as_string();

    RTF_ASSERT(responseBody == "Hello, Webby!");

    th.join();

    if(serverException)
        std::rethrow_exception(serverException);
}

void test_r_http::test_client_response_streaming()
{
    int port = RTF_NEXT_PORT();
    std::exception_ptr serverException;

    auto th = thread([&](){
        try {
            r_socket socket;
            socket.bind(port);
            socket.listen();

            auto clientSocket = socket.accept();

            r_server_request ssRequest;
            ssRequest.read_request(clientSocket);

            r_server_response ssResponse;
            ssResponse.set_content_type("application/x-rtsp-tunnelled");

            for(uint8_t i = 0; i < 10; ++i)
                ssResponse.write_chunk(clientSocket, 1, &i);

            ssResponse.write_chunk_finalizer(clientSocket);
        } catch(...) {
            serverException = std::current_exception();
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));

    r_socket socket;
    socket.connect("127.0.0.1", port);

    string message = "Hello, Webby!";

    r_client_request request("127.0.0.1", port);
    request.set_method(METHOD_POST);
    request.set_body(message);

    request.write_request(socket);

    uint8_t sum = 0;

    r_client_response response;
    response.register_chunk_callback([&](const vector<uint8_t>& chunk, const r_client_response&){
        sum += chunk[0];
    },
    true);
    response.read_response(socket);

    // For this test the server sends back 10 chunks, each 1 byte long... Containing the values
    // 9 down to 0. The sum of 0 .. 9 == 45. Our callback just sums them, so at this point our
    // _total should == 45.
    RTF_ASSERT(sum == 45);

    th.join();

    if(serverException)
        std::rethrow_exception(serverException);
}

void test_r_http::test_client_response_100_continue()
{
    int port = RTF_NEXT_PORT();
    std::exception_ptr serverException;

    auto th = thread([&](){
        try {
            r_socket socket;
            socket.bind(port);
            socket.listen();

            auto clientSocket = socket.accept();

            r_server_request ssRequest;
            ssRequest.read_request(clientSocket);

            auto body = ssRequest.get_body_as_string();

            string cont = "HTTP/1.1 100 Continue\r\n\r\n";
            clientSocket.send(cont.c_str(), cont.length());

            r_server_response ssResponse;
            ssResponse.set_body(body);
            ssResponse.write_response(clientSocket);
        } catch(...) {
            serverException = std::current_exception();
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));

    r_socket socket;
    socket.connect("127.0.0.1", port);

    string message = "Hello, Webby!";

    r_client_request request("127.0.0.1", port);
    request.set_method(METHOD_POST);
    request.set_body(message);

    request.write_request(socket);

    r_client_response response;
    response.read_response(socket);

    auto responseBody = response.get_body_as_string();

    RTF_ASSERT(responseBody == "Hello, Webby!");

    th.join();

    if(serverException)
        std::rethrow_exception(serverException);
}

void test_r_http::test_client_response_multi_part()
{
    int port = RTF_NEXT_PORT();
    std::exception_ptr serverException;
    std::exception_ptr clientException;

    auto th = thread([&](){
        try {
            r_socket socket;
            socket.bind(port);
            socket.listen();

            auto clientSocket = socket.accept();

            r_server_request ssRequest;
            ssRequest.read_request(clientSocket);

            r_server_response ssResponse;
            ssResponse.set_content_type("multipart/x-mixed-replace; boundary=\"foo\"");

            ssResponse.write_response(clientSocket);

            for(uint8_t i = 0; i < 10; ++i)
            {
                map<string,string> headers;
                headers.insert(make_pair("Content-Type", "image/jpeg"));
                ssResponse.write_part(clientSocket, "foo", headers, &i, 1);
            }

            ssResponse.write_part_finalizer( clientSocket, "foo");
        } catch(...) {
            serverException = std::current_exception();
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));

    try {
        r_socket socket;
        socket.connect("127.0.0.1", port);

        string message = "Hello, Webby!";

        r_client_request request("127.0.0.1", port);
        request.set_method(METHOD_POST);
        request.set_body(message);

        request.write_request(socket);

        uint8_t sum = 0;

        r_client_response response;
        response.register_part_callback([&](const std::vector<uint8_t>& chunk, const std::map<std::string, string>&, const r_client_response&){
            sum += chunk[0];
        });

        response.read_response(socket);

        RTF_ASSERT(sum == 45);
    } catch(...) {
        clientException = std::current_exception();
    }

    th.join();

    if(serverException)
        std::rethrow_exception(serverException);
    if(clientException)
        std::rethrow_exception(clientException);
}

void test_r_http::test_client_request_chunked_single()
{
    // Tests r_client_request chunked sending AND r_server_request chunked receiving
    int port = RTF_NEXT_PORT();
    std::exception_ptr serverException;

    auto th = thread([&](){
        try {
            r_socket socket;
            socket.bind(port);
            socket.listen();

            auto clientSocket = socket.accept();

            // Use r_server_request to parse the chunked request
            r_server_request ssRequest;
            ssRequest.read_request(clientSocket);

            // Verify the request was parsed correctly
            RTF_ASSERT(ssRequest.get_method() == METHOD_POST);
            RTF_ASSERT(ssRequest.get_uri().get_full_raw_uri() == "/chunked-test");
            RTF_ASSERT(ssRequest.get_body_as_string() == "Hello, Chunked!");

            // Send response
            r_server_response ssResponse;
            ssResponse.set_body("OK");
            ssResponse.write_response(clientSocket);
        } catch(...) {
            serverException = std::current_exception();
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));

    r_socket socket;
    socket.connect("127.0.0.1", port);

    r_client_request request("127.0.0.1", port);
    request.set_method(METHOD_POST);
    request.set_uri("/chunked-test");
    request.set_content_type("text/plain");

    string message = "Hello, Chunked!";
    request.write_chunk(socket, message.length(), message.c_str());
    request.write_chunk_finalizer(socket);

    r_client_response response;
    response.read_response(socket);

    RTF_ASSERT(response.get_body_as_string() == "OK");

    th.join();

    if(serverException)
        std::rethrow_exception(serverException);
}

void test_r_http::test_client_request_chunked_multiple()
{
    // Tests r_client_request sending multiple chunks AND r_server_request receiving with callback
    int port = RTF_NEXT_PORT();
    std::exception_ptr serverException;

    auto th = thread([&](){
        try {
            r_socket socket;
            socket.bind(port);
            socket.listen();

            auto clientSocket = socket.accept();

            // Use r_server_request with callback to verify each chunk arrives correctly
            r_server_request ssRequest;

            int chunkCount = 0;
            char expectedChar = 'A';
            ssRequest.register_chunk_callback([&](const vector<uint8_t>& chunk, const r_server_request&){
                RTF_ASSERT(chunk.size() == 1);
                RTF_ASSERT(chunk[0] == expectedChar);
                expectedChar++;
                chunkCount++;
            });

            ssRequest.read_request(clientSocket);

            // Verify all 10 chunks were received
            RTF_ASSERT(chunkCount == 10);
            RTF_ASSERT(ssRequest.get_method() == METHOD_PUT);

            // Send response
            r_server_response ssResponse;
            ssResponse.set_body("OK");
            ssResponse.write_response(clientSocket);
        } catch(...) {
            serverException = std::current_exception();
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));

    r_socket socket;
    socket.connect("127.0.0.1", port);

    r_client_request request("127.0.0.1", port);
    request.set_method(METHOD_PUT);
    request.set_uri("/multi-chunk");
    request.set_content_type("application/octet-stream");

    // Send 10 single-byte chunks
    for(char c = 'A'; c <= 'J'; ++c)
    {
        request.write_chunk(socket, 1, &c);
    }
    request.write_chunk_finalizer(socket);

    r_client_response response;
    response.read_response(socket);

    RTF_ASSERT(response.get_body_as_string() == "OK");

    th.join();

    if(serverException)
        std::rethrow_exception(serverException);
}

void test_r_http::test_server_request_chunked_accumulate()
{
    // Test that r_server_request properly accumulates chunked body when no callback is registered
    int port = RTF_NEXT_PORT();
    std::exception_ptr serverException;

    auto th = thread([&](){
        try {
            r_socket socket;
            socket.bind(port);
            socket.listen();

            auto clientSocket = socket.accept();

            // Use r_server_request to read the chunked request (no callback = accumulate)
            r_server_request ssRequest;
            ssRequest.read_request(clientSocket);

            // Verify the accumulated body
            RTF_ASSERT(ssRequest.get_method() == METHOD_POST);
            RTF_ASSERT(ssRequest.get_body_as_string() == "Hello, Chunked!");

            // Send response
            r_server_response ssResponse;
            ssResponse.set_body("OK");
            ssResponse.write_response(clientSocket);
        } catch(...) {
            serverException = std::current_exception();
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));

    r_socket socket;
    socket.connect("127.0.0.1", port);

    r_client_request request("127.0.0.1", port);
    request.set_method(METHOD_POST);
    request.set_uri("/chunked-test");
    request.set_content_type("text/plain");

    string message = "Hello, Chunked!";
    request.write_chunk(socket, message.length(), message.c_str());
    request.write_chunk_finalizer(socket);

    r_client_response response;
    response.read_response(socket);

    RTF_ASSERT(response.get_body_as_string() == "OK");

    th.join();

    if(serverException)
        std::rethrow_exception(serverException);
}

void test_r_http::test_server_request_chunked_callback()
{
    // Test that r_server_request properly calls callback for each chunk
    int port = RTF_NEXT_PORT();
    std::exception_ptr serverException;

    auto th = thread([&](){
        try {
            r_socket socket;
            socket.bind(port);
            socket.listen();

            auto clientSocket = socket.accept();

            // Use r_server_request with a callback
            r_server_request ssRequest;

            uint16_t sum = 0;
            ssRequest.register_chunk_callback([&](const vector<uint8_t>& chunk, const r_server_request&){
                // Each chunk is a single byte 'A' through 'J'
                sum += chunk[0];
            });

            ssRequest.read_request(clientSocket);

            // Verify callback was called correctly
            // 'A'=65, 'B'=66, ..., 'J'=74
            // Sum = 65+66+67+68+69+70+71+72+73+74 = 695
            RTF_ASSERT(sum == 695);

            // Verify body is empty (callback mode doesn't accumulate)
            RTF_ASSERT(ssRequest.get_body_size() == 0);

            // Send response
            r_server_response ssResponse;
            ssResponse.set_body("OK");
            ssResponse.write_response(clientSocket);
        } catch(...) {
            serverException = std::current_exception();
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));

    r_socket socket;
    socket.connect("127.0.0.1", port);

    r_client_request request("127.0.0.1", port);
    request.set_method(METHOD_PUT);
    request.set_uri("/chunked-callback");
    request.set_content_type("application/octet-stream");

    // Send 10 single-byte chunks
    for(char c = 'A'; c <= 'J'; ++c)
    {
        request.write_chunk(socket, 1, &c);
    }
    request.write_chunk_finalizer(socket);

    r_client_response response;
    response.read_response(socket);

    RTF_ASSERT(response.get_body_as_string() == "OK");

    th.join();

    if(serverException)
        std::rethrow_exception(serverException);
}
