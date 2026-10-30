
#include "test_r_http.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_ssl_socket.h"
#include "r_utils/r_ssl_server_socket.h"
#include "r_http/r_client_request.h"
#include "r_http/r_server_request.h"
#include "r_http/r_client_response.h"
#include "r_http/r_server_response.h"
#include "r_http/r_web_server.h"

#include <chrono>
#include <thread>
#ifndef IS_WINDOWS
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
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

static bool _wait_for(std::function<bool()> pred, int millis = 3000)
{
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis);
    while(!pred())
    {
        if(std::chrono::steady_clock::now() > until)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

// A one-worker server whose /block route waits until released, and whose
// /count route counts the requests it runs.
struct blocking_server
{
    std::mutex m;
    std::condition_variable cv;
    bool release {false};
    std::atomic<int> blocked {0}, counted {0};
    r_web_server<r_socket> ws;

    blocking_server(int port, size_t max_pending) : ws(port, "127.0.0.1", 1, max_pending)
    {
        ws.add_route(METHOD_GET, "/block", [this](const r_web_server<r_socket>&, r_socket&, const r_server_request&){
            ++blocked;
            std::unique_lock<std::mutex> g(m);
            cv.wait(g, [this](){ return release; });
            return r_server_response();
        });
        ws.add_route(METHOD_GET, "/count", [this](const r_web_server<r_socket>&, r_socket&, const r_server_request&){
            ++counted;
            return r_server_response();
        });
        ws.start();
    }
    ~blocking_server()
    {
        unblock();
        ws.stop();
    }
    void unblock()
    {
        {
            std::lock_guard<std::mutex> g(m);
            release = true;
        }
        cv.notify_all();
    }
};

static void _send_get(r_socket& sok, int port, const std::string& path)
{
    sok.connect("127.0.0.1", port);
    r_client_request request("127.0.0.1", port);
    request.set_uri(path);
    request.write_request(sok);
}

// A request that waited in the queue while its client gave up is not run:
// nobody would read the response.
void test_r_http::test_web_server_skips_abandoned_request()
{
    int port = RTF_NEXT_PORT();
    blocking_server server(port, 4);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    r_socket a;
    _send_get(a, port, "/block");
    RTF_ASSERT(_wait_for([&](){ return server.blocked == 1; }));

    // Queued behind /block, then abandoned.
    {
        r_socket gone;
        _send_get(gone, port, "/count");
        gone.close();
    }

    // Queued behind /block, and still waiting for its answer.
    r_socket waiting;
    _send_get(waiting, port, "/count");

    server.unblock();

    r_client_response response;
    response.read_response(waiting);
    RTF_ASSERT(response.get_status() == 200);
    RTF_ASSERT(server.counted == 1);
}

// With the worker busy and the queue full, a new connection is answered with
// 503 and Retry-After instead of waiting.
void test_r_http::test_web_server_busy_503()
{
    int port = RTF_NEXT_PORT();
    blocking_server server(port, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    r_socket a;
    _send_get(a, port, "/block");
    RTF_ASSERT(_wait_for([&](){ return server.blocked == 1; }));

    r_socket queued;
    _send_get(queued, port, "/count");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Refused with nothing sent yet...
    r_socket refused;
    refused.connect("127.0.0.1", port);
    r_client_response busy;
    busy.read_response(refused);
    RTF_ASSERT(busy.get_status() == 503);
    RTF_ASSERT(busy.get_header("Retry-After") == "1");

    // ...and after sending a request (the usual case): the server reads it
    // before answering, so the client gets the 503 rather than a reset.
    r_socket refused2;
    _send_get(refused2, port, "/count");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    r_client_response busy2;
    busy2.read_response(refused2);
    RTF_ASSERT(busy2.get_status() == 503);
    // The server closed in order (FIN, read returns 0), not with a reset.
    char c;
    uint64_t wait_ms = 2000;
    RTF_ASSERT(refused2.wait_till_recv_wont_block(wait_ms));
    RTF_ASSERT(refused2.recv(&c, 1) == 0);

    server.unblock();

    r_client_response ok;
    ok.read_response(queued);
    RTF_ASSERT(ok.get_status() == 200);
    RTF_ASSERT(server.counted == 1);
}

// Started and stopped (or destroyed) straight away, over and over, including
// before the accept loop is listening: no hang, no std::terminate.
void test_r_http::test_web_server_immediate_start_stop()
{
    int port = RTF_NEXT_PORT();
    for(int i = 0; i < 20; ++i)
    {
        r_web_server<r_socket> ws(port, "127.0.0.1");
        ws.start();
        if(i % 2)
            ws.stop();
    }
    {
        r_web_server<r_socket> never_started(port, "127.0.0.1");
    }

    // And it still serves afterwards.
    r_web_server<r_socket> ws(port, "127.0.0.1");
    std::atomic<int> counted {0};
    ws.add_route(METHOD_GET, "/count", [&](const r_web_server<r_socket>&, r_socket&, const r_server_request&){
        ++counted;
        return r_server_response();
    });
    ws.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    r_socket sok;
    _send_get(sok, port, "/count");
    r_client_response response;
    response.read_response(sok);
    RTF_ASSERT(response.get_status() == 200);
    RTF_ASSERT(counted == 1);
}

// The port is already taken, so the server never comes up. Destroying it must
// still join its thread: a joinable std::thread member aborted revere on quit.
void test_r_http::test_web_server_port_taken()
{
    int port = RTF_NEXT_PORT();
    r_socket holder;
    holder.bind(port, "127.0.0.1");
    holder.listen();

    {
        r_web_server<r_socket> ws(port, "127.0.0.1");
        ws.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    {
        r_web_server<r_socket> ws(port, "127.0.0.1");
        ws.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ws.stop();
        ws.stop();
    }
}

// Destroyed while a handler is still working: the destructor waits for it
// (so the handler never outlives the server's state) and doesn't throw.
void test_r_http::test_web_server_destroyed_with_request_in_flight()
{
    int port = RTF_NEXT_PORT();
    std::atomic<int> entered {0}, finished {0};
    r_socket a, b;
    {
        r_web_server<r_socket> ws(port, "127.0.0.1", 1, 4);
        ws.add_route(METHOD_GET, "/slow", [&](const r_web_server<r_socket>&, r_socket&, const r_server_request&){
            ++entered;
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            ++finished;
            return r_server_response();
        });
        ws.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        _send_get(a, port, "/slow");                 // running
        RTF_ASSERT(_wait_for([&](){ return entered == 1; }));
        _send_get(b, port, "/slow");                 // queued behind it
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // stop() returns only once the running handler is done (r_ws relies on
        // this to tear down state its handlers use); the queued one is dropped.
        ws.stop();
        RTF_ASSERT(finished == 1);
    }
    RTF_ASSERT(finished == 1);
    RTF_ASSERT(entered == 1);
}

// The busy answer reads what a refused client has sent, on the accept thread,
// for at most 50 ms. A client trickling a byte every half millisecond mustn't
// stretch that: the next refused client still gets its 503 straight away.
void test_r_http::test_web_server_busy_503_trickling_client()
{
    int port = RTF_NEXT_PORT();
    blocking_server server(port, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    r_socket a;
    _send_get(a, port, "/block");
    RTF_ASSERT(_wait_for([&](){ return server.blocked == 1; }));
    r_socket queued;
    _send_get(queued, port, "/count");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Refused, and sends one byte every 0.5 ms for 3 s.
    std::atomic<bool> trickling {true};
    std::thread slow([&](){
        r_socket s;
        s.connect("127.0.0.1", port);
#ifndef IS_WINDOWS
        int one = 1;
        setsockopt(s.get_sok_id(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        char b = 'x';
        while(trickling && std::chrono::steady_clock::now() < until)
        {
            if(s.send(&b, 1) <= 0)
                break;
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // A normal client refused right after it.
    auto t0 = std::chrono::steady_clock::now();
    r_socket refused;
    _send_get(refused, port, "/count");
    r_client_response busy;
    busy.read_response(refused);
    auto took = std::chrono::steady_clock::now() - t0;
    trickling = false;
    slow.join();

    RTF_ASSERT(busy.get_status() == 503);
    RTF_ASSERT(took < std::chrono::milliseconds(500));
}


// ---------------------------------------------------------------------------
// HTTPS: r_web_server over r_ssl_server_socket (what r_ws's TLS listener is)
// ---------------------------------------------------------------------------

static std::shared_ptr<r_utils::r_ssl_server_config> _test_tls_config()
{
    auto pems = r_utils::r_ssl_server_config::generate_self_signed("r_http_ut");
    return std::make_shared<r_utils::r_ssl_server_config>(pems.first, pems.second);
}

// The same route table served over TLS: the request is read through
// r_socket_base (handshake completes on the first read), the handler runs on a
// worker, and the response goes back encrypted. Client is the existing
// r_ssl_socket in its no-verify mode, as vision will use.
void test_r_http::test_web_server_https()
{
    int port = RTF_NEXT_PORT();
    auto cfg = _test_tls_config();

    r_web_server<r_utils::r_ssl_server_socket> ws(port, "127.0.0.1", 2);
    ws.get_socket().set_config(cfg);
    std::atomic<int> hits {0};
    ws.add_route(METHOD_GET, "/hello", [&](const r_web_server<r_utils::r_ssl_server_socket>&, r_utils::r_ssl_server_socket& conn, const r_server_request& req){
        ++hits;
        RTF_ASSERT(conn.handshake_done());
        RTF_ASSERT(conn.get_peer_ip() == "127.0.0.1");
        r_server_response response;
        response.set_content_type("text/plain");
        auto who = req.get_header("x-who");
        response.set_body("hello " + (who.is_null() ? std::string("?") : who.value()));
        return response;
    });
    ws.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Sequential requests, then a burst of concurrent ones.
    for(int i = 0; i < 3; ++i)
    {
        r_utils::r_ssl_socket sok(false);
        sok.connect("127.0.0.1", port);
        r_client_request request("127.0.0.1", port);
        request.set_uri("/hello");
        request.add_header("x-who", "seq" + std::to_string(i));
        request.write_request(sok);
        r_client_response response;
        response.read_response(sok);
        RTF_ASSERT(response.is_success());
        RTF_ASSERT(response.get_body_as_string().value() == "hello seq" + std::to_string(i));
    }

    const int N = 5;
    std::atomic<int> ok {0};
    std::vector<std::thread> clients;
    for(int i = 0; i < N; ++i)
    {
        clients.emplace_back([&, i](){
            try
            {
                r_utils::r_ssl_socket sok(false);
                sok.connect("127.0.0.1", port);
                r_client_request request("127.0.0.1", port);
                request.set_uri("/hello");
                request.add_header("x-who", "par" + std::to_string(i));
                request.write_request(sok);
                r_client_response response;
                response.read_response(sok);
                if(response.is_success() && response.get_body_as_string().value() == "hello par" + std::to_string(i))
                    ++ok;
            }
            catch(...) {}
        });
    }
    for(auto& c : clients)
        c.join();

    ws.stop();
    RTF_ASSERT(ok == N);
    RTF_ASSERT(hits == 3 + N);
}

// A client that speaks plaintext HTTP to the TLS port is dropped without
// taking a worker down or wedging the server: a TLS client right after it is
// still served.
void test_r_http::test_web_server_https_survives_plaintext_probe()
{
    int port = RTF_NEXT_PORT();
    auto cfg = _test_tls_config();

    r_web_server<r_utils::r_ssl_server_socket> ws(port, "127.0.0.1", 1);   // one worker: a wedge would show
    ws.get_socket().set_config(cfg);
    ws.add_route(METHOD_GET, "/ping", [&](const r_web_server<r_utils::r_ssl_server_socket>&, r_utils::r_ssl_server_socket&, const r_server_request&){
        r_server_response response;
        response.set_body("pong");
        return response;
    });
    ws.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        r_socket plain;
        plain.connect("127.0.0.1", port);
        std::string junk = "GET /ping HTTP/1.1\r\nHost: x\r\n\r\n";
        plain.send(junk.c_str(), junk.size());
        // The server fails the handshake and closes; we should see EOF (or a
        // reset), never a plaintext reply.
        uint64_t ms = 3000;
        char buf[64];
        int got = plain.wait_till_recv_wont_block(ms) ? plain.recv(buf, sizeof(buf)) : 0;
        RTF_ASSERT(got <= 0);
    }

    r_utils::r_ssl_socket sok(false);
    sok.connect("127.0.0.1", port);
    r_client_request request("127.0.0.1", port);
    request.set_uri("/ping");
    request.write_request(sok);
    r_client_response response;
    response.read_response(sok);
    RTF_ASSERT(response.is_success());
    RTF_ASSERT(response.get_body_as_string().value() == "pong");

    ws.stop();
}

// Busy over HTTPS: a refused connection is handled on the accept thread, so a
// client that never starts its TLS handshake must not hold that thread for the
// connection's 10 s I/O timeout. It's closed within the 50 ms budget (plus the
// 503's own), and the accept thread goes on to refuse the next client properly.
void test_r_http::test_web_server_https_busy_silent_client()
{
    int port = RTF_NEXT_PORT();
    auto cfg = _test_tls_config();

    std::mutex m;
    std::condition_variable cv;
    bool release = false;
    std::atomic<int> blocked {0};
    r_web_server<r_utils::r_ssl_server_socket> ws(port, "127.0.0.1", 1, 1);
    ws.get_socket().set_config(cfg);
    ws.add_route(METHOD_GET, "/block", [&](const r_web_server<r_utils::r_ssl_server_socket>&, r_utils::r_ssl_server_socket&, const r_server_request&){
        ++blocked;
        std::unique_lock<std::mutex> g(m);
        cv.wait(g, [&](){ return release; });
        return r_server_response();
    });
    // Released on the way out, pass or fail, so the server can stop.
    struct releaser {
        std::mutex& m; std::condition_variable& cv; bool& release;
        ~releaser() { { std::lock_guard<std::mutex> g(m); release = true; } cv.notify_all(); }
    } releaser_on_exit {m, cv, release};
    ws.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // The one worker busy...
    r_utils::r_ssl_socket a(false);
    a.connect("127.0.0.1", port);
    r_client_request block("127.0.0.1", port);
    block.set_uri("/block");
    block.write_request(a);
    RTF_ASSERT(_wait_for([&](){ return blocked == 1; }));

    // ...and the one queue place taken.
    r_socket queued;
    queued.connect("127.0.0.1", port);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Refused, and silent: no ClientHello, ever.
    auto start = std::chrono::steady_clock::now();
    r_socket silent;
    silent.connect("127.0.0.1", port);
    char c;
    uint64_t wait_ms = 5000;
    bool closed = silent.wait_till_recv_wont_block(wait_ms) && silent.recv(&c, 1) <= 0;
    auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    RTF_ASSERT(closed);
    RTF_ASSERT(took < 2000);

    // The accept thread is free: a real HTTPS client right after gets its 503.
    r_utils::r_ssl_socket next(false);
    next.connect("127.0.0.1", port);
    r_client_request request("127.0.0.1", port);
    request.set_uri("/block");
    request.write_request(next);
    r_client_response busy;
    busy.read_response(next);
    RTF_ASSERT(busy.get_status() == 503);
    RTF_ASSERT(busy.get_header("Retry-After") == "1");

    queued.close();
}
