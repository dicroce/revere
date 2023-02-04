
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

    auto th = std::thread([port](){
        r_socket socket;
        socket.bind(port);
        socket.listen();

        auto clientSocket = socket.accept();

        r_server_request request;

        request.read_request(clientSocket);

        RTF_ASSERT(request.get_method() == METHOD_GET);

        r_uri u = request.get_uri();

        RTF_ASSERT(u.get_full_raw_uri() == "/vmas/device/status");
    });

    std::this_thread::sleep_for(std::chrono::microseconds(1000000));

    r_socket socket;
    socket.connect("127.0.0.1", port);

    r_client_request request("127.0.0.1", port);

    request.set_uri("/vmas/device/status");

    request.write_request(socket);

    th.join();
}

void test_r_http::test_client_response_default_constructor()
{
    r_client_response response;
}

void test_r_http::test_client_response_receive()
{
    int port = RTF_NEXT_PORT();

    auto th = thread([&](){
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
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));

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
}

void test_r_http::test_client_response_streaming()
{
    int port = RTF_NEXT_PORT();

    auto th = thread([&](){

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
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));

    r_socket socket;
    socket.connect("127.0.0.1", port);

    string message = "Hello, Webby!";

    r_client_request request("127.0.0.1", port);
    request.set_method(METHOD_POST);
    request.set_body(message);

    request.write_request(socket);

    uint8_t sum = 0;

    r_client_response response;
    response.register_chunk_callback([&](const vector<uint8_t>& chunk, const r_client_response& response){
        sum += chunk[0];
    },
    true);
    response.read_response(socket);

    // For this test the server sends back 10 chunks, each 1 byte long... Containing the values
    // 9 down to 0. The sum of 0 .. 9 == 45. Our callback just sums them, so at this point our
    // _total should == 45.
    RTF_ASSERT(sum == 45);

    th.join();
}

void test_r_http::test_client_response_100_continue()
{
    int port = RTF_NEXT_PORT();

    auto th = thread([&](){
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
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));

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
}

void test_r_http::test_client_response_multi_part()
{
    int port = RTF_NEXT_PORT();

    auto th = thread([&](){
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
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));

    r_socket socket;
    socket.connect("127.0.0.1", port);

    string message = "Hello, Webby!";

    r_client_request request("127.0.0.1", port);
    request.set_method(METHOD_POST);
    request.set_body(message);

    request.write_request(socket);

    uint8_t sum = 0;

    r_client_response response;
    response.register_part_callback([&](const std::vector<uint8_t>& chunk, const std::map<std::string, string>& headers, const r_client_response& response){
        sum += chunk[0];
    });

    response.read_response(socket);

    RTF_ASSERT(sum == 45);

    th.join();
}
