
#include "test_r_utils.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_weekly_schedule.h"
#include "r_utils/r_stack_trace.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_file.h"
#include "r_utils/r_file_lock.h"
#include "r_utils/r_md5.h"
#include "r_utils/r_sha1.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_ssl_socket.h"
#include "r_utils/r_udp_receiver.h"
#include "r_utils/r_udp_sender.h"
#include "r_utils/r_byte_ptr.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_uuid.h"
#include "r_utils/r_blob_tree.h"
#include "r_utils/r_work_q.h"
#include "r_utils/r_timer.h"
#include "r_utils/r_avg.h"
#include "r_utils/r_algorithms.h"
#include <chrono>
#include <thread>
#include <climits>
#include <numeric>
#include <cstdint>

using namespace std;
using namespace std::chrono;
using namespace r_utils;

REGISTER_TEST_FIXTURE(test_r_utils);

#ifdef WIN32
#pragma warning(push)
#pragma warning(disable: 4127)
#endif

void test_r_utils::setup()
{
    r_raw_socket::socket_startup();
}

void test_r_utils::teardown()
{
    r_raw_socket::socket_cleanup();
}

void test_r_utils::test_string_utils_split()
{
    {
        auto parts = r_string_utils::split("This is a test string.", "test");
        RTF_ASSERT(parts.size() == 2);
        RTF_ASSERT(parts[0] == "This is a ");
        RTF_ASSERT(parts[1] == " string.");
    }
    {
        auto parts = r_string_utils::split("This is a test string.", "BOOM");
        RTF_ASSERT(parts.size() == 1);
        RTF_ASSERT(parts[0] == "This is a test string.");
    }
    {
        auto parts = r_string_utils::split("This is a test string.", ' ');
        RTF_ASSERT(parts.size() == 5);
        RTF_ASSERT(parts[0] == "This");
        RTF_ASSERT(parts[1] == "is");
        RTF_ASSERT(parts[2] == "a");
        RTF_ASSERT(parts[3] == "test");
        RTF_ASSERT(parts[4] == "string.");
    }
    {
        auto parts = r_string_utils::split("foo-bar-baz", "foo-bar-baz");
        RTF_ASSERT(parts.size() == 0);
    }
    {
        auto parts = r_string_utils::split("---This---is---a---test---string.", "---");
        RTF_ASSERT(parts.size() == 5);
        RTF_ASSERT(parts[0] == "This");
        RTF_ASSERT(parts[1] == "is");
        RTF_ASSERT(parts[2] == "a");
        RTF_ASSERT(parts[3] == "test");
        RTF_ASSERT(parts[4] == "string.");

        parts = r_string_utils::split("This---is---a---test---string.---", "---");
        RTF_ASSERT(parts.size() == 5);
        RTF_ASSERT(parts[0] == "This");
        RTF_ASSERT(parts[1] == "is");
        RTF_ASSERT(parts[2] == "a");
        RTF_ASSERT(parts[3] == "test");
        RTF_ASSERT(parts[4] == "string.");

        parts = r_string_utils::split("---This---is---a---test---string.---", "---");
        RTF_ASSERT(parts.size() == 5);
        RTF_ASSERT(parts[0] == "This");
        RTF_ASSERT(parts[1] == "is");
        RTF_ASSERT(parts[2] == "a");
        RTF_ASSERT(parts[3] == "test");
        RTF_ASSERT(parts[4] == "string.");
    }
}

void test_r_utils::test_string_utils_basic_format()
{
    auto str = r_string_utils::format( "I am %d years old and my name is %s.", 44, "Tony");
    RTF_ASSERT(str == "I am 44 years old and my name is Tony.");
}

void test_r_utils::test_string_utils_basic_format_buffer()
{
    char buffer[4096];
    auto truncated = r_string_utils::format_buffer(buffer, 4096, "I am %d years old and my name is %s.", 41, "Tony");
    RTF_ASSERT(truncated == false);
    RTF_ASSERT(string(buffer) == "I am 41 years old and my name is Tony.");
}

void test_r_utils::test_string_utils_numeric_conversion()
{
    {
        // int
        RTF_ASSERT(r_string_utils::s_to_int("-32768") == -32768);
        RTF_ASSERT(r_string_utils::int_to_s(-32768) == "-32768");
        RTF_ASSERT(r_string_utils::s_to_int("32767") == 32767);
        RTF_ASSERT(r_string_utils::int_to_s(32767) == "32767");
    }

    {
        // 8 bit
        RTF_ASSERT(r_string_utils::s_to_uint8("0") == 0);
        RTF_ASSERT(r_string_utils::uint8_to_s(0) == "0");
        RTF_ASSERT(r_string_utils::s_to_uint8("255") == 255);
        RTF_ASSERT(r_string_utils::uint8_to_s(255) == "255");
        RTF_ASSERT(r_string_utils::s_to_int8("-128") == -128);
        RTF_ASSERT(r_string_utils::int8_to_s(-128) == "-128");
        RTF_ASSERT(r_string_utils::s_to_int8("127") == 127);
        RTF_ASSERT(r_string_utils::int8_to_s(127) == "127");
    }

    {
        // 16 bit
        RTF_ASSERT(r_string_utils::s_to_uint16("0") == 0);
        RTF_ASSERT(r_string_utils::uint16_to_s(0) == "0");
        RTF_ASSERT(r_string_utils::s_to_uint16("65535") == 65535);
        RTF_ASSERT(r_string_utils::uint16_to_s(65535) == "65535");
        RTF_ASSERT(r_string_utils::s_to_int16("-32768") == -32768);
        RTF_ASSERT(r_string_utils::int16_to_s(-32768) == "-32768");
        RTF_ASSERT(r_string_utils::s_to_int16("32767") == 32767);
        RTF_ASSERT(r_string_utils::int16_to_s(32767) == "32767");
    }

    {
        // 32 bit
        RTF_ASSERT(r_string_utils::s_to_uint32("0") == 0);
        RTF_ASSERT(r_string_utils::uint32_to_s(0) == "0");
        RTF_ASSERT(r_string_utils::s_to_uint32("4294967295") == 4294967295);
        RTF_ASSERT(r_string_utils::uint32_to_s(4294967295) == "4294967295");
#ifdef IS_WINDOWS
        RTF_ASSERT(r_string_utils::s_to_int32("-2147483648") == -2147483647 - 1);
        RTF_ASSERT(r_string_utils::int32_to_s(-2147483647 - 1) == "-2147483648");
#else
        RTF_ASSERT(r_string_utils::s_to_int32("-2147483648") == -2147483648);
        RTF_ASSERT(r_string_utils::int32_to_s(-2147483648) == "-2147483648");
#endif
        RTF_ASSERT(r_string_utils::s_to_int32("2147483647") == 2147483647);
        RTF_ASSERT(r_string_utils::int32_to_s(2147483647) == "2147483647");
    }

    {
        // 64 bit
        RTF_ASSERT(r_string_utils::s_to_uint64("0") == 0);
        RTF_ASSERT(r_string_utils::uint64_to_s(0) == "0");
        RTF_ASSERT(r_string_utils::s_to_uint64("18446744073709551615") == 18446744073709551615LLU);
        RTF_ASSERT(r_string_utils::uint64_to_s(18446744073709551615LLU) == "18446744073709551615");

#ifdef IS_WINDOWS
        RTF_ASSERT(r_string_utils::s_to_int64("-9223372036854775808") == LLONG_MIN);
        RTF_ASSERT(r_string_utils::int64_to_s(LLONG_MIN) == "-9223372036854775808");
        RTF_ASSERT(r_string_utils::s_to_int64("9223372036854775807") == LLONG_MAX);
        RTF_ASSERT(r_string_utils::int64_to_s(LLONG_MAX) == "9223372036854775807");
#else
        RTF_ASSERT(r_string_utils::s_to_int64("-9223372036854775808") == LONG_LONG_MIN);
        RTF_ASSERT(r_string_utils::int64_to_s(LONG_LONG_MIN) == "-9223372036854775808");
        RTF_ASSERT(r_string_utils::s_to_int64("9223372036854775807") == LONG_LONG_MAX);
        RTF_ASSERT(r_string_utils::int64_to_s(LONG_LONG_MAX) == "9223372036854775807");
#endif
    }
}

void test_r_utils::test_string_utils_base64_encode()
{
    uint8_t buf[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    const string encodedData = r_string_utils::to_base64(&buf[0], sizeof(buf));

    RTF_ASSERT(encodedData == "AQIDBAUGBwgJCg==");
}

void test_r_utils::test_string_utils_base64_decode()
{
    uint8_t buf[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    const string encodedData = "AQIDBAUGBwgJCg==";
    auto decoded = r_string_utils::from_base64(encodedData);

    RTF_ASSERT(memcmp(buf, &decoded[0], 10) == 0);
}

void test_r_utils::test_stack_trace()
{
    auto s = r_stack_trace::get_stack();
    RTF_ASSERT(!s.empty());
}

void test_1()
{
    R_THROW(("Hey, some shiz went wrong!"));
}

void test_2(){test_1();}
void test_3(){test_2();}
void test_4(){test_3();}
void test_5(){test_4();}
void test_6(){test_5();}
void test_7(){test_6();}
void test_8(){test_7();}
void test_9(){test_8();}
void test_10(){test_9();}
void test_11(){test_10();}
void test_12(){test_11();}
void test_13(){test_12();}
void test_14(){test_13();}
void test_15(){test_14();}

void test_r_utils::test_stack_trace_exception()
{
    string what;
    try
    {
        test_15();
    }
    catch(const std::exception& e)
    {
        what = e.what();
    }

    RTF_ASSERT(what.length() > 0);
}

void test_r_utils::test_read_file()
{
    {
        auto f = r_file::open("foo.txt", "w+");
        //can use libc file calls on f
        fprintf(f, "Hello %s!\n", "World");
    }

    {
        auto fb = r_fs::read_file("foo.txt");
        string s((char*)&fb[0], fb.size());
        RTF_ASSERT(r_string_utils::contains(s, "Hello World!"));
    }

    r_fs::remove_file("foo.txt");
}

void test_r_utils::test_stat()
{
    {
        auto f = r_file::open("foo.txt", "w+");
        //can use libc file calls on f
        fprintf(f, "Hello %s!\n", "World");
    }

    {
        r_fs::r_file_info rfi;
        r_fs::stat("foo.txt", &rfi);
#ifdef IS_WINDOWS
        RTF_ASSERT(rfi.file_size == 14); // windows has CR/LF for \n so it needs one more byte
#else
        RTF_ASSERT(rfi.file_size == 13);
#endif
    }

    r_fs::remove_file("foo.txt");
}

void test_r_utils::test_file_lock()
{
    {
        auto lockFile = r_file::open("lockfile", "w+");
        fprintf(lockFile, "Hello %s!\n", "World");
    }

    {
        auto lockFile = r_file::open("lockfile", "r+");
        auto otherFile = r_file::open("lockfile", "r+");

        int state = 42;

        r_file_lock fileLock(r_fs::fileno(lockFile));

        thread t;

        {
            r_file_lock_guard g(fileLock);

            t = thread([&](){
                    r_file_lock newLock(r_fs::fileno(otherFile));
                    r_file_lock_guard g(newLock);
                    state = 43;
                });

            std::this_thread::sleep_for(std::chrono::microseconds(250000));

            RTF_ASSERT(state == 42);
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100000));

        RTF_ASSERT(state == 43);

        t.join();
    }

    r_fs::remove_file("lockfile");
}

void test_r_utils::test_shared_file_lock()
{
    {
        auto lockFile = r_file::open("lockfile", "w+");
        fprintf(lockFile, "Hello %s!\n", "World");
    }

    {
        auto lockFile = r_file::open("lockfile", "r+");

        auto otherFileA = r_file::open( "lockfile", "r+" );
        auto otherFileB = r_file::open( "lockfile", "r+" );

        int state = 42;  // initially state is 42,

        // But then we fire up two threads with a shared lock

        thread t1([&](){
                r_file_lock newLock( r_fs::fileno( otherFileA ) );
                r_file_lock_guard g( newLock, false );
                ++state;
                std::this_thread::sleep_for(std::chrono::microseconds(500000));
            });

        thread t2([&](){
                r_file_lock newLock( r_fs::fileno( otherFileB ) );
                r_file_lock_guard g( newLock, false );
                ++state;
                std::this_thread::sleep_for(std::chrono::microseconds(500000));
            });

        std::this_thread::sleep_for(std::chrono::microseconds(250000));

        {
            r_file_lock newLock( r_fs::fileno( lockFile ) );
            r_file_lock_guard g( newLock );
            // since the above shared locks must be let go before an exclusive can be acquired, then we know at this point
            // state should be 44.
            RTF_ASSERT( state == 44 );
        }

        t2.join();
        t1.join();
    }

    r_fs::remove_file("lockfile");
}

void test_r_utils::test_md5_basic()
{
    r_md5 md5;
    string msg = "Beneath this mask there is an idea, Mr. Creedy, and ideas are bulletproof.";
    md5.update((uint8_t*)msg.c_str(), msg.length());
    md5.finalize();
    auto output = md5.get_as_string();
    RTF_ASSERT(output == "68cc4c2cbf04714ffd2b4306376410b8");
}

void test_r_utils::test_client_server()
{
    const vector<string> addrs = {"127.0.0.1"};

    int port = RTF_NEXT_PORT();

    for( auto addr : addrs )
    {
        thread t([&](){
            r_socket server_sok;
            server_sok.bind( port, addr );
            server_sok.listen();
            auto connected = server_sok.accept();

            unsigned int val = 0;
            connected.recv(&val, 4);
            val+=1;
            connected.send(&val, 4);
        });

        std::this_thread::sleep_for(std::chrono::microseconds(250000));

        unsigned int val = 41;
        r_socket client_sok;

        client_sok.connect(addr, port);

        // Test get_peer_ip()
        RTF_ASSERT_NO_THROW( client_sok.get_peer_ip() );
        RTF_ASSERT( client_sok.get_peer_ip() != "0.0.0.0" );

        // Test get_local_ip()
        RTF_ASSERT_NO_THROW( client_sok.get_local_ip() );
        RTF_ASSERT( client_sok.get_local_ip() != "0.0.0.0" );

        client_sok.send( &val, 4 );
        client_sok.recv( &val, 4 );

        RTF_ASSERT( val == 42 );
        t.join();
    }
}

void test_r_utils::test_socket_move_constructable()
{
    r_socket sokA;
    sokA.connect( "www.google.com", 80 );
    string query = "GET /\r\n\r\n";
    r_socket sokB = std::move(sokA); // move ctor
    r_networking::r_send(sokB, query.c_str(), query.length());
    char buffer[1024];
    memset( buffer, 0, 1024 );
    r_networking::r_recv(sokB, buffer, 1023);
    sokB.close();
    string response = buffer;
    RTF_ASSERT( r_string_utils::contains(response, "google") );
    RTF_ASSERT( sokA._sok._sok == -1 );
}

void test_r_utils::test_socket_move_assignable()
{
    r_socket sokA;
    sokA.connect( "www.google.com", 80 );
    string query = "GET /\r\n\r\n";
    r_socket sokB;
    sokB = std::move(sokA); // move assignment
    r_networking::r_send(sokB, query.c_str(), query.length());
    char buffer[1024];
    memset( buffer, 0, 1024 );
    r_networking::r_recv(sokB, buffer, 1023);
    sokB.close();
    string response = buffer;
    RTF_ASSERT( r_string_utils::contains(response, "google") );
    RTF_ASSERT( sokA._sok._sok == -1 );
}

void test_r_utils::test_socket_close_warm_socket()
{
    int port = RTF_NEXT_PORT();

    thread t([&](){
        r_socket serverSocket;

        serverSocket.bind( port, "127.0.0.1" );

        serverSocket.listen();

        auto clientSocket = serverSocket.accept();

        uint32_t val = 0;
        clientSocket.recv( &val, 4 );

        val = val + 1;

        clientSocket.send( &val, 4 );
    });

    std::this_thread::sleep_for(std::chrono::microseconds(250000));
    r_socket socket;

    RTF_ASSERT_NO_THROW( socket.connect( "127.0.0.1", port ) );

    uint32_t val = 41;

    socket.send( &val, 4 );

    socket.recv( &val, 4 );

    RTF_ASSERT( val == 42 );

    socket.close();

    RTF_ASSERT( socket._sok._sok == -1 );

    t.join();
}

void test_r_utils::test_socket_wont_block()
{
    int port = RTF_NEXT_PORT();

    thread t([&](){
        r_socket server_sok;
        server_sok.bind( port );
        server_sok.listen();
        auto connected = server_sok.accept();
        unsigned int val = 13;
        uint64_t timeout = 5000;
        if( connected.wait_till_send_wont_block( timeout ) )
            connected.send( &val, 4 );
    });

    std::this_thread::sleep_for(std::chrono::microseconds(250000));

    bool read_thirteen = false;

    r_socket client_sok;
    client_sok.connect( "127.0.0.1", port );
    uint64_t timeout = 5000;
    if( client_sok.wait_till_recv_wont_block( timeout ) )
    {
        unsigned int val = 0;
        client_sok.recv( &val, 4 );
        if( val == 13 )
            read_thirteen = true;
    }
    
    RTF_ASSERT( read_thirteen == true );

    t.join();
}

void test_r_utils::test_buffered()
{
    r_socket sok;
    sok.connect( "www.google.com", 80 );

    string query = "GET /\r\n\r\n";
    r_networking::r_send(sok, query.c_str(), query.length());

    char buffer[1024];
    memset( buffer, 0, 1024 );
    r_networking::r_recv(sok, buffer, 1023);

    sok.close();
    string response = buffer;
    RTF_ASSERT( r_string_utils::contains(response, "google") );
}

void test_r_utils::test_ssl_socket_connect_to_example_com()
{
    r_utils::r_ssl_socket sock(true);

    sock.connect("example.com", 443);

    RTF_ASSERT(sock.valid());

    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: close\r\n"
        "\r\n";

    r_networking::r_send(sock, request.data(), request.size());

    uint8_t buffer[4096] = {0};
    int received = r_networking::r_recv(sock, buffer, 1024);

    printf("buffer: %s\n", buffer);

    std::string response((char*)buffer);

    // Basic validation that we got an HTTP response
    RTF_ASSERT(response.find("HTTP/1.1") != std::string::npos);
    RTF_ASSERT(response.find("200 OK") != std::string::npos);
}

void test_r_utils::test_ssl_socket_connect_to_google()
{
    r_utils::r_ssl_socket sock(true);

    sock.connect("www.google.com", 443);

    RTF_ASSERT(sock.valid());

    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: www.google.com\r\n"
        "Connection: close\r\n"
        "\r\n";

    r_networking::r_send(sock, request.data(), request.size());

    uint8_t buffer[4096] = {0};
    int received = r_networking::r_recv(sock, buffer, 1024);

    std::string response((char*)buffer);

    // Basic validation that we got an HTTP response
    RTF_ASSERT(response.find("HTTP/1.1") != std::string::npos);
    // Google might return various status codes (200, 301, etc.) but should at least be HTTP
    RTF_ASSERT(r_string_utils::contains(response, "google"));
}

void test_r_utils::test_ssl_socket_certificate_validation()
{
    // Test that certificate validation works correctly
    
    // Test 1: Valid certificate should work with auth enabled
    {
        r_utils::r_ssl_socket sock(true); // enable_auth = true
        RTF_ASSERT_NO_THROW(sock.connect("www.google.com", 443));
        RTF_ASSERT(sock.valid());
        sock.close();
    }
    
    // Test 2: Self-signed certificate should fail with auth enabled
    {
        r_utils::r_ssl_socket sock(true); // enable_auth = true
        try {
            sock.connect("self-signed.badssl.com", 443);
            // If we get here, the connection succeeded when it shouldn't have
            RTF_ASSERT(false && "Connection to self-signed cert should have failed");
        } catch (const std::exception&) {
            // Expected - connection should fail due to invalid cert
            RTF_ASSERT(!sock.valid());
        }
    }
    
    // Test 3: Self-signed certificate should work with auth disabled
    {
        r_utils::r_ssl_socket sock(false); // enable_auth = false
        RTF_ASSERT_NO_THROW(sock.connect("self-signed.badssl.com", 443));
        RTF_ASSERT(sock.valid());
        sock.close();
    }
    
    // Test 4: Expired certificate should fail with auth enabled
    {
        r_utils::r_ssl_socket sock(true); // enable_auth = true
        try {
            sock.connect("expired.badssl.com", 443);
            RTF_ASSERT(false && "Connection to expired cert should have failed");
        } catch (const std::exception&) {
            // Expected - connection should fail
            RTF_ASSERT(!sock.valid());
        }
    }
    
    // Test 5: Wrong host certificate should fail with auth enabled
    {
        r_utils::r_ssl_socket sock(true); // enable_auth = true
        try {
            sock.connect("wrong.host.badssl.com", 443);
            RTF_ASSERT(false && "Connection to wrong host cert should have failed");
        } catch (const std::exception&) {
            // Expected - connection should fail
            RTF_ASSERT(!sock.valid());
        }
    }
}

void test_r_utils::test_ssl_socket_no_auth()
{
    // Test SSL socket creation without authentication
    r_utils::r_ssl_socket sock(false);

    // Connect to a test server (badssl.com provides various test cases)
    RTF_ASSERT_NO_THROW(sock.connect("badssl.com", 443));

    RTF_ASSERT(sock.valid());

    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: badssl.com\r\n"
        "Connection: close\r\n"
        "\r\n";

    r_networking::r_send(sock, request.data(), request.size());

    uint8_t buffer[1024] = {0};
    int received = r_networking::r_recv(sock, buffer, sizeof(buffer));
    RTF_ASSERT(received > 0);

    sock.close();
    RTF_ASSERT(!sock.valid());
}

void test_r_utils::test_ssl_socket_destructor()
{
    // Test that destructor properly cleans up resources
    {
        r_utils::r_ssl_socket sock(false);
        sock.connect("example.com", 443);
        RTF_ASSERT(sock.valid());
        // Socket should be cleaned up when going out of scope
    }
    
    // Test destructor on unconnected socket
    {
        r_utils::r_ssl_socket sock(false);
        // Should not crash
    }
}

void test_r_utils::test_ssl_socket_send_recv()
{
    r_utils::r_ssl_socket sock(false);
    sock.connect("httpbin.org", 443);

    RTF_ASSERT(sock.valid());

    // Test sending data
    const std::string request =
        "GET /get HTTP/1.1\r\n"
        "Host: httpbin.org\r\n"
        "Connection: close\r\n"
        "\r\n";

    int sent = r_networking::r_send(sock, request.data(), request.size());
    RTF_ASSERT(sent == static_cast<int>(request.size()));

    // Test receiving data
    uint8_t buffer[4096] = {0};
    int received = r_networking::r_recv(sock, buffer, sizeof(buffer));
    RTF_ASSERT(received > 0);

    std::string response((char*)buffer, received);
    RTF_ASSERT(response.find("HTTP/1.1 200 OK") != std::string::npos);

    // Test partial receives (may be 0 if server closed connection after first response)
    uint8_t small_buffer[64];
    int small_received = r_networking::r_recv(sock, small_buffer, sizeof(small_buffer));
    RTF_ASSERT(small_received >= 0 && small_received <= static_cast<int>(sizeof(small_buffer)));
}

void test_r_utils::test_ssl_socket_invalid_connection()
{
    // Test operations on unconnected socket
    {
        r_utils::r_ssl_socket sock(false);
        uint8_t buffer[100];
        RTF_ASSERT(sock.send(buffer, sizeof(buffer)) < 0);
        RTF_ASSERT(sock.recv(buffer, sizeof(buffer)) < 0);
        RTF_ASSERT(!sock.valid());
    }
    
    // Test connection to likely closed port
    {
        r_utils::r_ssl_socket sock(false);
        try {
            sock.connect("127.0.0.1", 54321); // Unlikely to be open
            // If it connects, just close it
            sock.close();
        } catch (const std::exception&) {
            // Expected to fail
            RTF_ASSERT(!sock.valid());
        }
    }
}

void test_r_utils::test_ssl_socket_wait_wont_block()
{
    r_utils::r_ssl_socket sock(false);
    sock.connect("httpbin.org", 443);
    
    // Test wait_till_send_wont_block - should return true immediately for send
    uint64_t send_timeout = 1000; // 1 second
    bool can_send = sock.wait_till_send_wont_block(send_timeout);
    RTF_ASSERT(can_send); // Should be able to send immediately
    
    // Send request
    const std::string request =
        "GET /get HTTP/1.1\r\n"
        "Host: httpbin.org\r\n"
        "Connection: close\r\n"
        "\r\n";
    sock.send(request.data(), request.size());
    
    // Test wait_till_recv_wont_block - wait for response
    uint64_t recv_timeout = 5000; // 5 seconds
    bool can_recv = sock.wait_till_recv_wont_block(recv_timeout);
    RTF_ASSERT(can_recv); // Should have data available within 5 seconds
    
    // Now we should be able to receive
    uint8_t buffer[1024];
    int received = sock.recv(buffer, sizeof(buffer));
    RTF_ASSERT(received > 0);
    
    // Verify we got HTTP response
    std::string response((char*)buffer, received);
    RTF_ASSERT(response.find("HTTP/1.1") != std::string::npos);
}

void test_r_utils::test_ssl_socket_get_ip_methods()
{
    r_utils::r_ssl_socket sock(false);
    sock.connect("example.com", 443);
    
    RTF_ASSERT(sock.valid());
    
    // Test get_peer_ip
    std::string peer_ip = sock.get_peer_ip();
    RTF_ASSERT(!peer_ip.empty());
    RTF_ASSERT(peer_ip != "0.0.0.0");
    RTF_ASSERT(peer_ip.find('.') != std::string::npos || peer_ip.find(':') != std::string::npos); // IPv4 or IPv6
    
    // Test get_local_ip
    std::string local_ip = sock.get_local_ip();
    RTF_ASSERT(!local_ip.empty());
    RTF_ASSERT(local_ip != "0.0.0.0");
    RTF_ASSERT(local_ip.find('.') != std::string::npos || local_ip.find(':') != std::string::npos); // IPv4 or IPv6
}

void test_r_utils::test_ssl_socket_multiple_close()
{
    r_utils::r_ssl_socket sock(false);
    sock.connect("example.com", 443);
    
    RTF_ASSERT(sock.valid());
    
    // First close
    sock.close();
    RTF_ASSERT(!sock.valid());
    
    // Multiple close calls should not crash
    RTF_ASSERT_NO_THROW(sock.close());
    RTF_ASSERT_NO_THROW(sock.close());
    
    // Operations after close should fail gracefully
    uint8_t buffer[100];
    RTF_ASSERT(sock.send(buffer, sizeof(buffer)) < 0);
    RTF_ASSERT(sock.recv(buffer, sizeof(buffer)) < 0);
}

void test_r_utils::test_udp_send()
{
    const char addrs[][32] = { "127.0.0.1", "::1" };

    for( int ii=0; ii<2; ++ii )
    {
        auto recvAddress = r_socket_address::get_address_family( addrs[ii] ) == AF_INET ? ip4_addr_any : ip6_addr_any;
        int val = 0;

        thread t([&](){
            vector<uint8_t> buffer;

            r_udp_receiver server( 8484, DEFAULT_UDP_RECV_BUF_SIZE, recvAddress );

            int destinationPort = 0;

            server.receive( destinationPort, buffer );

            r_byte_ptr_rw reader(&buffer[0], buffer.size());

            val = reader.consume<int>();
        });

        std::this_thread::sleep_for(std::chrono::microseconds(500000));

        vector<uint8_t> buffer(4);

        r_byte_ptr_rw writer(&buffer[0], 4);

        writer.write<int>(42);

        r_udp_sender client( addrs[ii], 8484 );

        client.send( &buffer[0], buffer.size() );

        std::this_thread::sleep_for(std::chrono::microseconds(500000));

        RTF_ASSERT( val == 42 );

        t.join();
    }
}

void test_r_utils::test_udp_associated_send()
{
    const char addrs[][32] = { "127.0.0.1", "::1" };

    for( int ii=0; ii<2; ++ii )
    {
        auto recvAddress = r_socket_address::get_address_family( addrs[ii] ) == AF_INET ? ip4_addr_any : ip6_addr_any;
        int val = 0;

        thread t([&](){
            vector<uint8_t> buffer;

            // Bind to a specific interface
            shared_ptr<r_udp_receiver> serverA = make_shared<r_udp_receiver>( 8787, DEFAULT_UDP_RECV_BUF_SIZE, recvAddress );
            shared_ptr<r_udp_receiver> serverB = make_shared<r_udp_receiver>( 8888, DEFAULT_UDP_RECV_BUF_SIZE, recvAddress );

            serverA->associate( serverB );

            int destinationPort = 0;

            serverA->receive( destinationPort, buffer );

            RTF_ASSERT( destinationPort == 8787 );

            r_byte_ptr_rw reader(&buffer[0], buffer.size());

            val += reader.consume<int>();

            serverA->receive( destinationPort, buffer );

            reader = r_byte_ptr_rw(&buffer[0], buffer.size());

            val += reader.consume<int>();

            RTF_ASSERT(destinationPort == 8888);
        });

        std::this_thread::sleep_for(std::chrono::microseconds(500000));

        vector<uint8_t> buffer(4);
        r_byte_ptr_rw writer(&buffer[0], buffer.size());
        writer.write<int>( 42 );

        r_udp_sender clientA( addrs[ii], 8787 );

        clientA.send( &buffer[0], buffer.size() );

        r_udp_sender clientB( addrs[ii], 8888 );

        clientB.send( &buffer[0], buffer.size() );

        std::this_thread::sleep_for(std::chrono::microseconds(100000));

        RTF_ASSERT( val == 84 );

        t.join();
    }
}

void test_r_utils::test_udp_get_set_recv_buffer_size()
{
    {
        r_udp_receiver r;
        RTF_ASSERT_NO_THROW( r.get_recv_buffer_size() );
        RTF_ASSERT_NO_THROW( r.set_recv_buffer_size(256 * 1024) );
    }
    {
        r_udp_receiver r;
        size_t sizeOrig = 0, sizeFinal = 0;
        RTF_ASSERT_NO_THROW( sizeOrig = r.get_recv_buffer_size() );
        RTF_ASSERT_NO_THROW( r.set_recv_buffer_size( sizeOrig * 2 ) );
        RTF_ASSERT_NO_THROW( sizeFinal = r.get_recv_buffer_size() );
        RTF_ASSERT( sizeFinal > sizeOrig );
    }
}

void test_r_utils::test_udp_test_shutdown_closed_while_blocked()
{
    const char addrs[][32] = { "127.0.0.1", "::1" };

    for( int ii=0; ii<2; ++ii )
    {
        auto recvAddress = r_socket_address::get_address_family( addrs[ii] ) == AF_INET ? ip4_addr_any : ip6_addr_any;
        int val = 0;

        auto receiver = make_shared<r_udp_receiver>( 8989, DEFAULT_UDP_RECV_BUF_SIZE, addrs[ii] );

        thread t([&](){
            vector<uint8_t> buffer(1600);

            int destinationPort = 0;

            receiver->receive( destinationPort, buffer );

            val = 1234;
        });

        std::this_thread::sleep_for(std::chrono::microseconds(1000000));

        receiver->shutdown();
        receiver->close();

        std::this_thread::sleep_for(std::chrono::microseconds(100000));

        RTF_ASSERT( val == 1234 );

        t.join();
    }
}

void test_r_utils::test_udp_raw_recv()
{
    const char addrs[][32] = { "127.0.0.1", "::1" };

    for( int ii=0; ii<2; ++ii )
    {
        auto recvAddress = r_socket_address::get_address_family( addrs[ii] ) == AF_INET ? ip4_addr_any : ip6_addr_any;
        int val = 0;

        thread t([&](){
            vector<uint8_t> buffer;

            r_udp_receiver server( 9090, DEFAULT_UDP_RECV_BUF_SIZE, recvAddress );

            int destinationPort = 0;

            server.raw_receive( destinationPort, buffer );

            r_byte_ptr_rw reader(&buffer[0], buffer.size());

            val = reader.consume<int>();
        });

        std::this_thread::sleep_for(std::chrono::microseconds(500000));

        vector<uint8_t> buffer(4);
        r_byte_ptr_rw writer(&buffer[0], buffer.size());
        writer.write<int>( 24 );

        r_udp_sender client( addrs[ii], 9090 );

        client.send( &buffer[0], buffer.size() );

        std::this_thread::sleep_for(std::chrono::microseconds(100000));

        RTF_ASSERT( val == 24 );

        t.join();
    }
}

void test_r_utils::test_udp_shutdown_close_while_blocked_in_raw_recv()
{
    const char addrs[][32] = { "127.0.0.1", "::1" };

    for( int ii=0; ii<2; ++ii )
    {
        auto recvAddress = r_socket_address::get_address_family( addrs[ii] ) == AF_INET ? ip4_addr_any : ip6_addr_any;
        int val = 0;

        auto receiver = make_shared<r_udp_receiver>( 9191, DEFAULT_UDP_RECV_BUF_SIZE, addrs[ii] );

        thread t([&](){
            vector<uint8_t> buffer(1600);

            int destinationPort = 0;

            receiver->raw_receive( destinationPort, buffer );

            val = 4321;
        });

        std::this_thread::sleep_for(std::chrono::microseconds(500000));

        receiver->close();

        std::this_thread::sleep_for(std::chrono::microseconds(100000));

        RTF_ASSERT( val == 4321 );

        t.join();
    }
}

void test_r_utils::test_8601_to_tp()
{
    string pts = "2021-03-13T20:00:00.000"; 
    auto ptp = r_time_utils::iso_8601_to_tp(pts);

    string ats = "2021-03-14T20:00:00.000"; // remember, this time is sprung ahead...
    auto atp = r_time_utils::iso_8601_to_tp(ats);

    int min = duration_cast<minutes>(atp-ptp).count();

    // not 1440 becuase that second time (being local) was "sprung ahead" and so the difference between them is less that 24 hours of minutes.
    RTF_ASSERT(min == 1380 || min == 1440);
}

void test_r_utils::test_tp_to_8601()
{
    // Local times can only be checked for general things (like non zero length) unless
    // you only want this test to run in a specific timezone.
    RTF_ASSERT(r_time_utils::tp_to_iso_8601(system_clock::from_time_t(1520740800), false).length() > 0);
    RTF_ASSERT(r_time_utils::tp_to_iso_8601(system_clock::from_time_t(1520712000), true) == "2018-03-10T20:00:00.000Z");
    RTF_ASSERT(r_time_utils::tp_to_iso_8601(system_clock::from_time_t(1520823600), false).length() > 0);
    RTF_ASSERT(r_time_utils::tp_to_iso_8601(system_clock::from_time_t(1520798400), true) == "2018-03-11T20:00:00.000Z");
}

void test_r_utils::test_uuid_generate()
{
    uint8_t buffer[16];
    memset(&buffer[0], 0, 16);
    r_uuid::generate(&buffer[0]);
    RTF_ASSERT(std::accumulate(&buffer[0], &buffer[16], 0) != 0);
    RTF_ASSERT(r_uuid::generate().length() == 36);
}

void test_r_utils::test_uuid_to_s()
{
    uint8_t buffer[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
#ifdef IS_WINDOWS
    auto s = r_uuid::uuid_to_s(&buffer[0]);
    RTF_ASSERT(r_uuid::uuid_to_s(&buffer[0]) == "03020100-0504-0706-0809-0a0b0c0d0e0f");
#endif
#ifdef IS_LINUX
    RTF_ASSERT(r_uuid::uuid_to_s(&buffer[0]) == "00010203-0405-0607-0809-0a0b0c0d0e0f");
#endif
}

void test_r_utils::test_s_to_uuid()
{
    uint8_t buffer[16];
    memset(&buffer[0], 0, 16);
    r_uuid::s_to_uuid("e158d3a3-bacd-45cb-92ea-4db749fe026c", buffer);
#ifdef IS_WINDOWS
    RTF_ASSERT(buffer[0] == 0xa3);
    RTF_ASSERT(buffer[1] == 0xd3);
    RTF_ASSERT(buffer[2] == 0x58);
    RTF_ASSERT(buffer[3] == 0xe1);
    RTF_ASSERT(buffer[4] == 0xcd);
    RTF_ASSERT(buffer[5] == 0xba);
    RTF_ASSERT(buffer[6] == 0xcb);
    RTF_ASSERT(buffer[7] == 0x45);
    RTF_ASSERT(buffer[8] == 0x92);
    RTF_ASSERT(buffer[9] == 0xea);
    RTF_ASSERT(buffer[10] == 0x4d);
    RTF_ASSERT(buffer[11] == 0xb7);
    RTF_ASSERT(buffer[12] == 0x49);
    RTF_ASSERT(buffer[13] == 0xfe);
    RTF_ASSERT(buffer[14] == 0x02);
    RTF_ASSERT(buffer[15] == 0x6c);
#endif
#ifdef IS_LINUX
    RTF_ASSERT(buffer[0] == 0xe1);
    RTF_ASSERT(buffer[1] == 0x58);
    RTF_ASSERT(buffer[2] == 0xd3);
    RTF_ASSERT(buffer[3] == 0xa3);
    RTF_ASSERT(buffer[4] == 0xba);
    RTF_ASSERT(buffer[5] == 0xcd);
    RTF_ASSERT(buffer[6] == 0x45);
    RTF_ASSERT(buffer[7] == 0xcb);
    RTF_ASSERT(buffer[8] == 0x92);
    RTF_ASSERT(buffer[9] == 0xea);
    RTF_ASSERT(buffer[10] == 0x4d);
    RTF_ASSERT(buffer[11] == 0xb7);
    RTF_ASSERT(buffer[12] == 0x49);
    RTF_ASSERT(buffer[13] == 0xfe);
    RTF_ASSERT(buffer[14] == 0x02);
    RTF_ASSERT(buffer[15] == 0x6c);
#endif
}

string hasher(const uint8_t* p, size_t size)
{
    r_md5 hash;
    hash.update(p, size);
    hash.finalize();
    return hash.get_as_string();
}

void test_r_utils::test_blob_tree_basic()
{
    vector<uint8_t> blob_1 = {1, 2, 3, 4};
    auto blob_1_hash = hasher(&blob_1[0], blob_1.size());
    vector<uint8_t> blob_2 = {5, 6, 7, 8};
    auto blob_2_hash = hasher(&blob_2[0], blob_2.size());
    vector<uint8_t> blob_3 = {9, 10, 11, 12};
    auto blob_3_hash = hasher(&blob_3[0], blob_3.size());
    vector<uint8_t> blob_4 = {13, 14, 15, 16};
    auto blob_4_hash = hasher(&blob_4[0], blob_4.size());

    r_blob_tree rt1;
    rt1["hello"]["my sweeties!"][0] = blob_1;
    rt1["hello"]["my sweeties!"][1] = blob_2;
    rt1["hello"]["my darlings!"][0] = blob_3;
    rt1["hello"]["my darlings!"][1] = blob_4;

    auto buffer = r_blob_tree::serialize(rt1, 42);

    uint32_t version;
    auto rt2 = r_blob_tree::deserialize(&buffer[0], buffer.size(), version);

    RTF_ASSERT(version == 42);

    RTF_ASSERT(rt2["hello"]["my sweeties!"].size() == 2);

    auto val = rt2["hello"]["my sweeties!"][0].get_blob();
    RTF_ASSERT(hasher(val.data(), val.size()) == blob_1_hash);

    val = rt2["hello"]["my sweeties!"][1].get_blob();
    RTF_ASSERT(hasher(val.data(), val.size()) == blob_2_hash);

    val = rt2["hello"]["my darlings!"][0].get_blob();
    RTF_ASSERT(hasher(val.data(), val.size()) == blob_3_hash);

    val = rt2["hello"]["my darlings!"][1].get_blob();
    RTF_ASSERT(hasher(val.data(), val.size()) == blob_4_hash);
}

void test_r_utils::test_blob_tree_objects_in_array()
{
    r_blob_tree rt1;

    string b1 = "We need a nine bit byte.";
    string b2 = "What if a building has more doors than their are guids?";
    string b3 = "If java compiles, its bug free.";
    string b4 = "Microsoft COM is a good idea.";

    // When used this way, r_blob_tree doesn't own the data.
    rt1[0]["obj_a"] = b1;
    rt1[0]["obj_b"] = b2;
    rt1[1]["obj_a"] = b3;
    rt1[1]["obj_b"] = b4;

    auto buffer = r_blob_tree::serialize(rt1, 42);

    uint32_t version;
    auto rt2 = r_blob_tree::deserialize(&buffer[0], buffer.size(), version);

    auto ab1 = rt2[0]["obj_a"].get_string();

    auto ab2 = rt2[0]["obj_b"].get_string();

    auto ab3 = rt2[1]["obj_a"].get_string();

    auto ab4 = rt2[1]["obj_b"].get_string();

    RTF_ASSERT(b1 == ab1);
    RTF_ASSERT(b2 == ab2);
    RTF_ASSERT(b3 == ab3);
    RTF_ASSERT(b4 == ab4);
}

void test_r_utils::test_blob_tree_big()
{
    vector<uint8_t> serialized;

    {
        r_blob_tree bt;
        vector<uint8_t> frame(234298);

        bt["frames"][0]["ts"] = r_string_utils::int64_to_s(42);
        bt["frames"][0]["data"] = frame;

        bt["video_codec_name"] = "h264";
        bt["video_codec_parameters"] = "blah";

        serialized = r_blob_tree::serialize(bt, 1);
    }

    printf("serialized.size()=%u\n",(unsigned int)serialized.size());

    {
        uint32_t version = 0;
        r_blob_tree bt = r_blob_tree::deserialize(serialized.data(), serialized.size(), version);

        RTF_ASSERT(bt.has_key("frames"));
        RTF_ASSERT(bt["frames"].size() > 0);
        RTF_ASSERT(bt["frames"][0].has_key("ts"));
        RTF_ASSERT(bt["frames"][0].has_key("data"));
        RTF_ASSERT(bt.has_key("video_codec_name"));
        RTF_ASSERT(bt.has_key("video_codec_parameters"));
    }


}

void test_r_utils::test_work_q_basic()
{
    r_work_q<int,int> wq;
    bool running = true;
    auto consumer_th = std::thread([&](){
        while(running)
        {
            auto maybe_cmd = wq.poll();
            if(!maybe_cmd.is_null())
            {
                auto cmd = maybe_cmd.take();
                cmd.second.set_value(cmd.first+1);
            }
        }
    });

    auto answer1 = wq.post(41);
    auto answer2 = wq.post(1);

    answer2.wait();

    RTF_ASSERT(answer1.valid());
    RTF_ASSERT(answer1.get() == 42);

    RTF_ASSERT(answer2.get() == 2);

    running = false;
    wq.wake();
    consumer_th.join();
}

void test_r_utils::test_work_q_timeout()
{
    r_work_q<int,int> wq;

    int64_t poll_time = 0;
    auto consumer_th = std::thread([&](){
        auto time_before_poll = std::chrono::system_clock::now();
        auto maybe_cmd = wq.poll(seconds(10));
        auto time_after_poll = std::chrono::system_clock::now();
        poll_time = duration_cast<milliseconds>(time_after_poll - time_before_poll).count();
    });

    this_thread::sleep_for(std::chrono::seconds(2));
    wq.post(42);

    consumer_th.join();

    RTF_ASSERT(abs(poll_time - 2000) < 1000);
}

void test_r_utils::test_timer_basic()
{
    r_timer t;

    auto current_time = std::chrono::steady_clock::now();

    int five_hundred_fired = 0;
    t.add_timed_event(current_time, milliseconds(500), [&](){++five_hundred_fired;}, false);
    int one_hundred_fired = 0;
    t.add_timed_event(current_time, milliseconds(100), [&](){++one_hundred_fired;}, false, true);
    int four_hundred_fired = 0;
    t.add_timed_event(current_time, milliseconds(400), [&](){++four_hundred_fired;}, false);

    t.update(milliseconds(100), current_time + milliseconds(125));
    RTF_ASSERT(one_hundred_fired == 1 && five_hundred_fired == 0 && four_hundred_fired == 0);

    t.update(milliseconds(100), current_time + milliseconds(425));
    RTF_ASSERT(one_hundred_fired == 1 && five_hundred_fired == 0 && four_hundred_fired == 1);

    t.update(milliseconds(100), current_time + milliseconds(900));
    RTF_ASSERT(one_hundred_fired == 1 && five_hundred_fired == 1 && four_hundred_fired == 2);

    t.update(milliseconds(100), current_time + milliseconds(1000));
    RTF_ASSERT(one_hundred_fired == 1 && five_hundred_fired == 2 && four_hundred_fired == 2);

    t.update(milliseconds(100), current_time + milliseconds(50000));
    RTF_ASSERT(one_hundred_fired == 1 && five_hundred_fired == 100 && four_hundred_fired == 125);
}

void test_r_utils::test_sha1_basic()
{
    {
        r_sha1 hash;
        string msg = "Beneath this mask there is an idea, Mr. Creedy, and ideas are bulletproof.";
        hash.update((uint8_t*)msg.c_str(), msg.length());
        hash.finalize();
        auto output = hash.get_as_string();
        RTF_ASSERT(output == "7a454aa45aa178935997fc89ff609ff374f9fff1");
    }

    {
        r_sha1 hash;
        string msg = "http://123.456.789.000/foo/bar http/1.1";
        hash.update((uint8_t*)msg.c_str(), msg.length());
        msg = "AAAAA";
        hash.update((uint8_t*)msg.c_str(), msg.length());
        hash.finalize();
        auto output = hash.get_as_string();
        RTF_ASSERT(output == "198454083d597888a068fffc06d5423a104df9fe");
    }
}

void test_r_utils::test_exp_avg()
{
    r_exp_avg<uint64_t> avg(1000000, 5);
    avg.update(1000000);
    avg.update(999990);
    avg.update(1000010);
    avg.update(1000000);
    avg.update(1000011);
    auto fstddev = avg.standard_deviation();

    r_exp_avg<uint64_t> avg2(1000000, 5);
    avg2.update(900000);
    avg2.update(1000000);
    avg2.update(1100000);
    avg2.update(1000000);
    avg2.update(800000);
    auto sstddev = avg2.standard_deviation();

    RTF_ASSERT(fstddev < sstddev);
}

static bool ufind(uint8_t* b, uint8_t* e, uint8_t target, bool exact=true)
{
    auto found = lower_bound_bytes(
        b,
        e,
        &target,
        1,
        [](const uint8_t* p, const uint8_t* ts){
            return *p < *ts?-1:*p > *ts?1:0;
        }
    );

    if(found == e)
        return false;
    else
    {
        return (exact)?*found == target:true;
    }
}

void test_r_utils::test_lower_bound_bytes()
{
    std::vector<uint8_t> v = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140};

    auto b = &v[0];
    auto e = &v[0] + v.size();

    // find first thing
    RTF_ASSERT(ufind(b, e, 10));
    // find middle thing
    RTF_ASSERT(ufind(b, e, 50));
    // find last thing
    RTF_ASSERT(ufind(b, e, 140));
    // find thing before first thing, should return first thing
    RTF_ASSERT(ufind(b, e, 5, false));
    // find thing after last thing should return false
    RTF_ASSERT(!ufind(b, e, 145));
}

void test_r_utils::test_weekly_schedule_basic()
{
    // Monday-Friday, 9am-5pm (8 hours)
    r_weekly_schedule schedule("09:00:00", chrono::hours(8), {1, 2, 3, 4, 5});

    // Create a known time point: a Wednesday at 10am local time
    // We'll use tm to construct a specific local time
    tm test_tm = {};
    test_tm.tm_year = 2025 - 1900;  // Year since 1900
    test_tm.tm_mon = 11;             // December (0-based)
    test_tm.tm_mday = 17;            // Wednesday Dec 17, 2025
    test_tm.tm_hour = 10;
    test_tm.tm_min = 0;
    test_tm.tm_sec = 0;
    test_tm.tm_isdst = -1;           // Let system determine DST

    time_t test_time = mktime(&test_tm);
    auto tp_in = system_clock::from_time_t(test_time);

    // Should be IN during work hours on a weekday
    RTF_ASSERT(schedule.query(tp_in) == r_schedule_state::INSIDE);

    // Same day at 6pm (after work hours)
    test_tm.tm_hour = 18;
    test_time = mktime(&test_tm);
    auto tp_out = system_clock::from_time_t(test_time);
    RTF_ASSERT(schedule.query(tp_out) == r_schedule_state::OUTSIDE);

    // Saturday at 10am (not a work day)
    test_tm.tm_mday = 20;  // Saturday Dec 20, 2025
    test_tm.tm_hour = 10;
    test_time = mktime(&test_tm);
    auto tp_weekend = system_clock::from_time_t(test_time);
    RTF_ASSERT(schedule.query(tp_weekend) == r_schedule_state::OUTSIDE);
}

void test_r_utils::test_weekly_schedule_span_midnight()
{
    // Night shift: 10pm to 6am (8 hours), Sunday-Thursday nights
    r_weekly_schedule schedule("22:00:00", chrono::hours(8), {7, 1, 2, 3, 4});

    // Sunday night at 11pm - should be IN
    tm test_tm = {};
    test_tm.tm_year = 2025 - 1900;
    test_tm.tm_mon = 11;
    test_tm.tm_mday = 21;  // Sunday Dec 21, 2025
    test_tm.tm_hour = 23;
    test_tm.tm_min = 0;
    test_tm.tm_sec = 0;
    test_tm.tm_isdst = -1;

    time_t test_time = mktime(&test_tm);
    auto tp_sunday_night = system_clock::from_time_t(test_time);
    RTF_ASSERT(schedule.query(tp_sunday_night) == r_schedule_state::INSIDE);

    // Monday morning at 3am - should still be IN (carry over from Sunday night)
    test_tm.tm_mday = 22;  // Monday Dec 22, 2025
    test_tm.tm_hour = 3;
    test_time = mktime(&test_tm);
    auto tp_monday_morning = system_clock::from_time_t(test_time);
    RTF_ASSERT(schedule.query(tp_monday_morning) == r_schedule_state::INSIDE);

    // Monday morning at 7am - should be OUT (past 6am)
    test_tm.tm_hour = 7;
    test_time = mktime(&test_tm);
    auto tp_monday_late = system_clock::from_time_t(test_time);
    RTF_ASSERT(schedule.query(tp_monday_late) == r_schedule_state::OUTSIDE);

    // Friday night at 11pm - should be OUT (Friday=5 is not in the schedule)
    test_tm.tm_mday = 26;  // Friday Dec 26, 2025
    test_tm.tm_hour = 23;
    test_time = mktime(&test_tm);
    auto tp_friday_night = system_clock::from_time_t(test_time);
    RTF_ASSERT(schedule.query(tp_friday_night) == r_schedule_state::OUTSIDE);
}

void test_r_utils::test_weekly_schedule_json()
{
    // Create a schedule and serialize it
    r_weekly_schedule schedule1("08:30:00", chrono::seconds(32400), {1, 2, 3, 4, 5});

    auto json = schedule1.to_json();

    // Deserialize into a new schedule
    r_weekly_schedule schedule2(json);

    // Test that they behave the same way
    tm test_tm = {};
    test_tm.tm_year = 2025 - 1900;
    test_tm.tm_mon = 11;
    test_tm.tm_mday = 17;  // Wednesday
    test_tm.tm_hour = 10;
    test_tm.tm_min = 0;
    test_tm.tm_sec = 0;
    test_tm.tm_isdst = -1;

    time_t test_time = mktime(&test_tm);
    auto tp = system_clock::from_time_t(test_time);

    RTF_ASSERT(schedule1.query(tp) == schedule2.query(tp));
    RTF_ASSERT(schedule2.query(tp) == r_schedule_state::INSIDE);

    // Also test construction from JSON string
    r_weekly_schedule schedule3;
    schedule3.from_json(json);
    RTF_ASSERT(schedule3.query(tp) == r_schedule_state::INSIDE);
}

void test_r_utils::test_weekly_schedule_empty()
{
    // Empty schedule should always return OUT
    r_weekly_schedule empty;

    auto now = system_clock::now();
    RTF_ASSERT(empty.query(now) == r_schedule_state::OUTSIDE);

    // Schedule with no days should return OUT
    r_weekly_schedule no_days("09:00:00", chrono::hours(8), {});
    RTF_ASSERT(no_days.query(now) == r_schedule_state::OUTSIDE);

    // Schedule with zero duration should return OUT
    r_weekly_schedule no_duration("09:00:00", chrono::seconds(0), {1, 2, 3, 4, 5});
    RTF_ASSERT(no_duration.query(now) == r_schedule_state::OUTSIDE);
}

#ifdef WIN32
#pragma warning(pop)
#endif
