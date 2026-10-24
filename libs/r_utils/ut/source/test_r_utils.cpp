
#include "test_r_utils.h"
#include <filesystem>
#include <algorithm>
#include "r_utils/r_string_utils.h"
#include "r_utils/r_weekly_schedule.h"
#include "r_utils/r_stack_trace.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_file.h"
#include "r_utils/r_file_lock.h"
#include "r_utils/r_instance_lock.h"
#include "r_utils/r_md5.h"
#include "r_utils/r_sha1.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_server_threaded.h"
#include "r_utils/r_ssl_socket.h"
#include "r_utils/r_ssl_server_socket.h"
#include "r_utils/r_local_api_token.h"
#include "r_utils/r_udp_receiver.h"
#include "r_utils/r_udp_sender.h"
#include "r_utils/r_udp_socket.h"
#include "r_utils/r_byte_ptr.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_uuid.h"
#include "r_utils/r_blob_tree.h"
#include "r_utils/r_work_q.h"
#include "r_utils/r_timer.h"
#include "r_utils/r_avg.h"
#include "r_utils/r_algorithms.h"
#include "r_utils/r_ring_buffer.h"
#include "r_utils/r_qr_code.h"
#include "r_utils/r_startup.h"
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <climits>
#include <numeric>
#include <cstdint>
#include <algorithm>
#ifdef IS_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

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

// get_stack() runs inside every r_exception constructor, so it has to be
// cheap: no per-frame subprocess (atos/addr2line) unless explicitly enabled.
// 200 captures well under a second is the budget; the old popen path took
// seconds for ONE capture.
void test_r_utils::test_stack_trace_is_cheap()
{
    RTF_ASSERT(!r_stack_trace::symbolize_enabled());

    auto start = std::chrono::steady_clock::now();
    std::string s;
    for(int i = 0; i < 200; ++i)
        s = r_stack_trace::get_stack();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    RTF_ASSERT(ms < 1000);

    // Still useful: frames carry the function name plus image/offset so they
    // can be symbolized offline.
    RTF_ASSERT(s.find("test_stack_trace_is_cheap") != std::string::npos);
    RTF_ASSERT(s.find("[base 0x") != std::string::npos);
}

// Exceptions are thrown on many threads at once. On Windows the trace goes
// through DbgHelp, which is single-threaded; unserialized SymInitialize /
// SymCleanup pairs corrupted the heap (0xc0000374) in production.
void test_r_utils::test_stack_trace_concurrent()
{
    std::atomic<int> named{0};
    std::vector<std::thread> threads;
    for(int t = 0; t < 8; ++t)
    {
        threads.emplace_back([&](){
            for(int i = 0; i < 100; ++i)
            {
                try { R_THROW(("concurrent")); }
                catch(const r_exception& ex)
                {
                    if(std::string(ex.what()).find("test_stack_trace_concurrent") != std::string::npos)
                        ++named;
                }
            }
        });
    }
    for(auto& th : threads)
        th.join();
    RTF_ASSERT(named == 800);
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

void test_r_utils::test_mkdir_p_concurrent()
{
    // Many threads making the same new directory at once (like two vision
    // windows starting together): none throws, and it exists afterwards.
    for(int round = 0; round < 20; ++round)
    {
        auto top = r_fs::temp_file_name(".", "mkd_");
        auto dir = top + PATH_SLASH + "logs" + PATH_SLASH;
        atomic<int> failures {0};
        atomic<bool> go {false};
        vector<thread> threads;
        for(int i = 0; i < 8; ++i)
        {
            threads.emplace_back([&](){
                while(!go) {}
                try { r_fs::mkdir_p(dir); }
                catch(...) { ++failures; }
            });
        }
        go = true;
        for(auto& t : threads)
            t.join();
        RTF_ASSERT(failures == 0);
        RTF_ASSERT(r_fs::is_dir(dir));
        r_fs::rmdir(top + PATH_SLASH + "logs");
        r_fs::rmdir(top);
    }
}

void test_r_utils::test_mkdir_p_over_file()
{
    // A file where the directory should be is still an error.
    auto path = r_fs::temp_file_name(".", "mkf_");
    {
        auto f = r_file::open(path, "w+");
        fprintf(f, "not a directory");
    }
    bool threw = false;
    try { r_fs::mkdir_p(path); }
    catch(const exception&) { threw = true; }
    RTF_ASSERT(threw);
    r_fs::remove_file(path);
}

// Files in dir, by name.
static vector<string> _files_in(const string& dir)
{
    vector<string> names;
    for(auto& e : std::filesystem::directory_iterator(dir))
        names.push_back(e.path().filename().string());
    sort(names.begin(), names.end());
    return names;
}

void test_r_utils::test_atomic_write_file()
{
    auto dir = r_fs::temp_file_name(".", "awf_");
    r_fs::mkdir(dir);
    auto path = dir + PATH_SLASH + "cfg.json";

    // New file.
    string first = "{\"a\": 1}";
    r_fs::atomic_write_file((const uint8_t*)first.data(), first.size(), path);
    auto fb = r_fs::read_file(path);
    RTF_ASSERT(string((char*)fb.data(), fb.size()) == first);

    // Replacing a longer file with a shorter one leaves exactly the new bytes.
    string longer(10000, 'x');
    r_fs::atomic_write_file((const uint8_t*)longer.data(), longer.size(), path);
    string shorter = "{}";
    r_fs::atomic_write_file((const uint8_t*)shorter.data(), shorter.size(), path);
    fb = r_fs::read_file(path);
    RTF_ASSERT(string((char*)fb.data(), fb.size()) == shorter);

    // No temp file is left behind.
    RTF_ASSERT(_files_in(dir) == vector<string>{"cfg.json"});

    // A write that can't happen (no such directory) throws and leaves nothing.
    auto missing = dir + PATH_SLASH + "nope" + PATH_SLASH + "cfg.json";
    bool threw = false;
    try { r_fs::atomic_write_file((const uint8_t*)first.data(), first.size(), missing); }
    catch(const exception&) { threw = true; }
    RTF_ASSERT(threw);
    RTF_ASSERT(_files_in(dir) == vector<string>{"cfg.json"});

    std::filesystem::remove_all(dir);
}

void test_r_utils::test_quarantine_file()
{
    auto dir = r_fs::temp_file_name(".", "qf_");
    r_fs::mkdir(dir);
    auto path = dir + PATH_SLASH + "cfg.json";

    string bad = "{\"camera_answers\": {\"cam";
    r_fs::write_file((const uint8_t*)bad.data(), bad.size(), path);
    auto moved = r_fs::quarantine_file(path);
    RTF_ASSERT(!r_fs::file_exists(path));
    RTF_ASSERT(r_string_utils::starts_with(moved, path + ".bad-"));
    auto fb = r_fs::read_file(moved);
    RTF_ASSERT(string((char*)fb.data(), fb.size()) == bad);

    // A second bad file in the same second doesn't replace the first.
    r_fs::write_file((const uint8_t*)bad.data(), bad.size(), path);
    auto moved2 = r_fs::quarantine_file(path);
    RTF_ASSERT(moved2 != moved);
    RTF_ASSERT(r_fs::file_exists(moved) && r_fs::file_exists(moved2));

    std::filesystem::remove_all(dir);
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

void test_r_utils::test_socket_connect_timeout()
{
    using namespace std::chrono;

    // A host that never answers (a TEST-NET-1 address: reserved, unroutable).
    // A blocking connect waited ~21 s for this on Windows.
    {
        r_socket sok;
        sok.set_io_timeout( 1000 );
        auto start = steady_clock::now();
        bool threw = false;
        try { sok.connect( "192.0.2.1", 80 ); }
        catch( r_socket_connect_exception& ) { threw = true; }
        auto elapsed = duration_cast<milliseconds>( steady_clock::now() - start ).count();
        RTF_ASSERT( threw );
        RTF_ASSERT( elapsed < 3000 );
    }

    // Nothing listening: refused at once, not after the timeout.
    {
        int port = RTF_NEXT_PORT();
        r_socket sok;
        sok.set_io_timeout( 5000 );
        auto start = steady_clock::now();
        bool threw = false;
        try { sok.connect( "127.0.0.1", port ); }
        catch( r_socket_connect_exception& ) { threw = true; }
        auto elapsed = duration_cast<milliseconds>( steady_clock::now() - start ).count();
        RTF_ASSERT( threw );
        RTF_ASSERT( elapsed < 4500 );
    }

    // A listener: connects, and the socket is blocking again for send/recv.
    {
        int port = RTF_NEXT_PORT();
        r_socket server_sok;
        server_sok.bind( port );
        server_sok.listen();

        thread t([&](){
            auto connected = server_sok.accept();
            unsigned int val = 13;
            connected.send( &val, 4 );
        });

        r_socket client_sok;
        client_sok.set_io_timeout( 2000 );
        client_sok.connect( "127.0.0.1", port );
        unsigned int val = 0;
        int got = 0;
        while( got < 4 )
        {
            int n = client_sok.recv( ((char*)&val) + got, 4 - got );
            if( n <= 0 )
                break;
            got += n;
        }
        RTF_ASSERT( got == 4 && val == 13 );
        t.join();
    }
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

// r_udp_socket has no bind of its own: bind the receiver through fd().
void test_r_utils::test_udp_socket_send_recv()
{
    r_udp_socket rx, tx;
    r_socket_address rx_addr(9393, "127.0.0.1");
    RTF_ASSERT(::bind(rx.fd(), rx_addr.get_sock_addr(), rx_addr.sock_addr_size()) == 0);

    // Fail rather than hang if nothing arrives.
#ifdef IS_WINDOWS
    DWORD timeout = 2000;
#else
    struct timeval timeout = {2, 0};
#endif
    setsockopt(rx.fd(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    const uint8_t out[4] = {1, 2, 3, 4};
    RTF_ASSERT(tx.sendto(out, sizeof(out), rx_addr) == 4);

    uint8_t in[16] = {};
    r_socket_address from(0, "127.0.0.1");
    RTF_ASSERT(rx.recvfrom(in, sizeof(in), from) == 4);
    RTF_ASSERT(memcmp(in, out, sizeof(out)) == 0);
    RTF_ASSERT(((struct sockaddr_in*)from.get_sock_addr())->sin_addr.s_addr == htonl(INADDR_LOOPBACK));
}

// A destroyed r_udp_socket gives its descriptor back: the next one opened is
// the same as before it. (Measured after a first one, in case first use opens
// a descriptor that stays open.)
void test_r_utils::test_udp_socket_releases_fd()
{
#ifndef IS_WINDOWS
    auto next_fd = [](){
        int fd = dup(1);
        close(fd);
        return fd;
    };

    { r_udp_socket first; }

    const int before = next_fd();
    { r_udp_socket s; }
    RTF_ASSERT(next_fd() == before);
#endif
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

// A closed receiver and a destroyed sender give their socket back: the next
// descriptor opened is the same one as before them. (The first use opens a
// descriptor that stays open, so measure after one of each.)
void test_r_utils::test_udp_close_releases_socket()
{
#ifndef IS_WINDOWS
    auto next_fd = [](){
        int fd = dup(1);
        close(fd);
        return fd;
    };

    {
        r_udp_receiver receiver( 9293, DEFAULT_UDP_RECV_BUF_SIZE, "127.0.0.1" );
        receiver.close();
        r_udp_sender sender( "127.0.0.1", 9293 );
    }

    const int before = next_fd();
    {
        r_udp_receiver receiver( 9292, DEFAULT_UDP_RECV_BUF_SIZE, "127.0.0.1" );
        receiver.close();
    }
    RTF_ASSERT( next_fd() == before );

    {
        r_udp_sender sender( "127.0.0.1", 9292 );
    }
    RTF_ASSERT( next_fd() == before );
#endif
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

// ---- malformed-input / security tests for r_blob_tree::deserialize ----
//
// deserialize() is the "other side of the wire": it must treat its input as
// hostile and fail with a thrown r_invalid_argument_exception rather than
// over-read, over-allocate, or blow the stack. Each test below crafts a
// deliberately malformed frame and asserts a clean throw.

namespace {

// A valid 8-byte header: magic "RBT0" (big-endian 0x52425430) + version 42.
static const uint8_t RBT_HEADER[8] = { 0x52, 0x42, 0x54, 0x30, 0x00, 0x00, 0x00, 0x2A };

std::vector<uint8_t> rbt_frame(std::initializer_list<uint8_t> body)
{
    std::vector<uint8_t> v(RBT_HEADER, RBT_HEADER + 8);
    v.insert(v.end(), body);
    return v;
}

} // namespace

void test_r_utils::test_blob_tree_reject_truncated_header()
{
    uint32_t version = 0;
    // Fewer than 8 bytes: not even room for magic + version.
    std::vector<uint8_t> buf = { 0x52, 0x42, 0x54 };
    RTF_ASSERT_THROWS(r_blob_tree::deserialize(buf.data(), buf.size(), version),
                      r_invalid_argument_exception);
}

void test_r_utils::test_blob_tree_reject_bad_magic()
{
    uint32_t version = 0;
    std::vector<uint8_t> buf = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x01, 0x02 };
    RTF_ASSERT_THROWS(r_blob_tree::deserialize(buf.data(), buf.size(), version),
                      r_invalid_argument_exception);
}

void test_r_utils::test_blob_tree_reject_bad_node_type()
{
    uint32_t version = 0;
    // Type byte 0x07 is outside {OBJECT,ARRAY,LEAF}.
    auto buf = rbt_frame({ 0x07 });
    RTF_ASSERT_THROWS(r_blob_tree::deserialize(buf.data(), buf.size(), version),
                      r_invalid_argument_exception);
}

void test_r_utils::test_blob_tree_reject_truncated_field()
{
    uint32_t version = 0;
    // LEAF whose 4-byte length is truncated to 2 bytes — the checked read must
    // throw instead of over-reading past the buffer.
    auto buf = rbt_frame({ 0x02 /*NT_LEAF*/, 0xFF, 0xFF });
    RTF_ASSERT_THROWS(r_blob_tree::deserialize(buf.data(), buf.size(), version),
                      r_invalid_argument_exception);
}

void test_r_utils::test_blob_tree_reject_giant_leaf_len()
{
    uint32_t version = 0;
    // LEAF claiming a 4 GiB payload with no payload present. Must throw rather
    // than assign(p, p + 0xFFFFFFFF) and walk off the heap.
    auto buf = rbt_frame({ 0x02 /*NT_LEAF*/, 0xFF, 0xFF, 0xFF, 0xFF });
    RTF_ASSERT_THROWS(r_blob_tree::deserialize(buf.data(), buf.size(), version),
                      r_invalid_argument_exception);
}

void test_r_utils::test_blob_tree_reject_giant_array_count()
{
    uint32_t version = 0;
    // ARRAY claiming ~4 billion elements in a 13-byte message. Must throw rather
    // than resize() the vector to 4 billion nodes (allocation bomb).
    auto buf = rbt_frame({ 0x01 /*NT_ARRAY*/, 0xFF, 0xFF, 0xFF, 0xFF });
    RTF_ASSERT_THROWS(r_blob_tree::deserialize(buf.data(), buf.size(), version),
                      r_invalid_argument_exception);
}

void test_r_utils::test_blob_tree_reject_giant_object_count()
{
    uint32_t version = 0;
    auto buf = rbt_frame({ 0x00 /*NT_OBJECT*/, 0xFF, 0xFF, 0xFF, 0xFF });
    RTF_ASSERT_THROWS(r_blob_tree::deserialize(buf.data(), buf.size(), version),
                      r_invalid_argument_exception);
}

void test_r_utils::test_blob_tree_reject_deep_nesting()
{
    uint32_t version = 0;
    // Nest arrays far deeper than the reader's depth cap: each level is
    // [NT_ARRAY][count=1]. Must throw (max depth) rather than recurse until the
    // stack overflows.
    std::vector<uint8_t> buf(RBT_HEADER, RBT_HEADER + 8);
    for(int i = 0; i < 200; ++i)
    {
        buf.push_back(0x01);                         // NT_ARRAY
        buf.push_back(0x00); buf.push_back(0x00);
        buf.push_back(0x00); buf.push_back(0x01);    // count = 1
    }
    buf.push_back(0x02);                             // innermost NT_LEAF
    buf.push_back(0x00); buf.push_back(0x00);
    buf.push_back(0x00); buf.push_back(0x00);        // len = 0
    RTF_ASSERT_THROWS(r_blob_tree::deserialize(buf.data(), buf.size(), version),
                      r_invalid_argument_exception);
}

void test_r_utils::test_blob_tree_valid_still_roundtrips()
{
    // Regression: after the hardening, well-formed data must still round-trip.
    r_blob_tree in;
    in["name"] = std::string("camera-1");
    in["frames"][0] = std::string("frame-a");
    in["frames"][1] = std::string("frame-b");

    auto buf = r_blob_tree::serialize(in, 7);

    uint32_t version = 0;
    r_blob_tree out;
    RTF_ASSERT_NO_THROW(out = r_blob_tree::deserialize(buf.data(), buf.size(), version));
    RTF_ASSERT(version == 7);
    RTF_ASSERT(out.has_key("name"));
    RTF_ASSERT(out.at("name").get_string() == "camera-1");
    RTF_ASSERT(out.at("frames").size() == 2);
    RTF_ASSERT(out["frames"][0].get_string() == "frame-a");
    RTF_ASSERT(out["frames"][1].get_string() == "frame-b");
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

void test_r_utils::test_ring_buffer_basic()
{
    r_ring_buffer<int> rb(5);

    RTF_ASSERT(rb.empty());
    RTF_ASSERT(rb.size() == 0);
    RTF_ASSERT(rb.capacity() == 5);
    RTF_ASSERT(!rb.full());

    rb.push(10);
    RTF_ASSERT(rb.size() == 1);
    RTF_ASSERT(!rb.empty());
    RTF_ASSERT(!rb.full());
    RTF_ASSERT(rb.at(0) == 10);
    RTF_ASSERT(rb.oldest() == 10);
    RTF_ASSERT(rb.newest() == 10);

    rb.push(20);
    rb.push(30);
    RTF_ASSERT(rb.size() == 3);
    RTF_ASSERT(rb.at(0) == 10);  // oldest
    RTF_ASSERT(rb.at(1) == 20);
    RTF_ASSERT(rb.at(2) == 30);  // newest
    RTF_ASSERT(rb.oldest() == 10);
    RTF_ASSERT(rb.newest() == 30);

    rb.clear();
    RTF_ASSERT(rb.empty());
    RTF_ASSERT(rb.size() == 0);

    // Test zero capacity throws
    bool threw_on_zero = false;
    try {
        r_ring_buffer<int> bad(0);
    } catch(const std::invalid_argument&) {
        threw_on_zero = true;
    }
    RTF_ASSERT(threw_on_zero);

    // Test out of range throws
    rb.push(1);
    bool threw_on_range = false;
    try {
        rb.at(1);
    } catch(const std::out_of_range&) {
        threw_on_range = true;
    }
    RTF_ASSERT(threw_on_range);
}

void test_r_utils::test_ring_buffer_overwrite()
{
    r_ring_buffer<int> rb(3);

    rb.push(1);
    rb.push(2);
    rb.push(3);
    RTF_ASSERT(rb.full());
    RTF_ASSERT(rb.size() == 3);
    RTF_ASSERT(rb.at(0) == 1);
    RTF_ASSERT(rb.at(1) == 2);
    RTF_ASSERT(rb.at(2) == 3);

    // Push one more - should overwrite oldest (1)
    rb.push(4);
    RTF_ASSERT(rb.full());
    RTF_ASSERT(rb.size() == 3);
    RTF_ASSERT(rb.at(0) == 2);  // oldest is now 2
    RTF_ASSERT(rb.at(1) == 3);
    RTF_ASSERT(rb.at(2) == 4);  // newest is 4
    RTF_ASSERT(rb.oldest() == 2);
    RTF_ASSERT(rb.newest() == 4);

    // Push more
    rb.push(5);
    rb.push(6);
    RTF_ASSERT(rb.at(0) == 4);
    RTF_ASSERT(rb.at(1) == 5);
    RTF_ASSERT(rb.at(2) == 6);
}

void test_r_utils::test_ring_buffer_iteration()
{
    r_ring_buffer<int> rb(5);

    rb.push(10);
    rb.push(20);
    rb.push(30);

    vector<int> collected;
    rb.for_each([&](const int& v) { collected.push_back(v); });

    RTF_ASSERT(collected.size() == 3);
    RTF_ASSERT(collected[0] == 10);  // oldest first
    RTF_ASSERT(collected[1] == 20);
    RTF_ASSERT(collected[2] == 30);  // newest last

    // Test iteration after wrap-around
    rb.push(40);
    rb.push(50);
    rb.push(60);  // This overwrites 10

    collected.clear();
    rb.for_each([&](const int& v) { collected.push_back(v); });

    RTF_ASSERT(collected.size() == 5);
    RTF_ASSERT(collected[0] == 20);  // oldest after wrap
    RTF_ASSERT(collected[4] == 60);  // newest
}

void test_r_utils::test_ring_buffer_count_if()
{
    r_ring_buffer<int> rb(5);

    rb.push(1);
    rb.push(2);
    rb.push(3);
    rb.push(4);
    rb.push(5);

    // Count evens
    auto even_count = rb.count_if([](const int& v) { return v % 2 == 0; });
    RTF_ASSERT(even_count == 2);

    // Count > 3
    auto gt3_count = rb.count_if([](const int& v) { return v > 3; });
    RTF_ASSERT(gt3_count == 2);

    // Count all
    auto all_count = rb.count_if([](const int&) { return true; });
    RTF_ASSERT(all_count == 5);

    // Count none
    auto none_count = rb.count_if([](const int&) { return false; });
    RTF_ASSERT(none_count == 0);
}

void test_r_utils::test_ring_buffer_last_n_match()
{
    r_ring_buffer<int> rb(5);

    rb.push(1);
    rb.push(2);
    rb.push(3);
    rb.push(4);
    rb.push(5);

    // Last 2 are 4,5 - both > 3
    RTF_ASSERT(rb.last_n_match(2, [](const int& v) { return v > 3; }));

    // Last 3 are 3,4,5 - not all > 3
    RTF_ASSERT(!rb.last_n_match(3, [](const int& v) { return v > 3; }));

    // Last 1 is 5 - is > 0
    RTF_ASSERT(rb.last_n_match(1, [](const int& v) { return v > 0; }));

    // n=0 should always return true
    RTF_ASSERT(rb.last_n_match(0, [](const int&) { return false; }));

    // n > size should return false
    RTF_ASSERT(!rb.last_n_match(6, [](const int&) { return true; }));

    // Test with struct (simulating motion detection use case)
    struct motion_sample {
        int64_t ts;
        bool is_significant;
    };

    r_ring_buffer<motion_sample> motion_rb(5);
    motion_rb.push({1000, false});
    motion_rb.push({2000, false});
    motion_rb.push({3000, true});
    motion_rb.push({4000, true});
    motion_rb.push({5000, true});

    // Last 3 should all be significant
    RTF_ASSERT(motion_rb.last_n_match(3, [](const motion_sample& s) { return s.is_significant; }));

    // Last 4 should NOT all be significant
    RTF_ASSERT(!motion_rb.last_n_match(4, [](const motion_sample& s) { return s.is_significant; }));

    // Last 2 should NOT be non-significant
    RTF_ASSERT(!motion_rb.last_n_match(2, [](const motion_sample& s) { return !s.is_significant; }));
}

void test_r_utils::test_ipv4_subnet_hosts()
{
    // /24 -> 254 usable hosts, .1 .. .254, excluding network/broadcast.
    {
        auto hosts = r_networking::r_ipv4_subnet_hosts("192.168.1.50", "255.255.255.0", 4096);
        RTF_ASSERT(hosts.size() == 254);
        RTF_ASSERT(hosts.front() == "192.168.1.1");
        RTF_ASSERT(hosts.back() == "192.168.1.254");
        // network (.0) and broadcast (.255) must not be present
        RTF_ASSERT(std::find(hosts.begin(), hosts.end(), "192.168.1.0") == hosts.end());
        RTF_ASSERT(std::find(hosts.begin(), hosts.end(), "192.168.1.255") == hosts.end());
    }

    // /22 -> 1022 usable hosts spanning .68.1 .. .71.254.
    {
        auto hosts = r_networking::r_ipv4_subnet_hosts("192.168.68.69", "255.255.252.0", 4096);
        RTF_ASSERT(hosts.size() == 1022);
        RTF_ASSERT(hosts.front() == "192.168.68.1");
        RTF_ASSERT(hosts.back() == "192.168.71.254");
        RTF_ASSERT(std::find(hosts.begin(), hosts.end(), "192.168.68.67") != hosts.end());
    }

    // Over the cap -> empty (don't sweep a huge range).
    {
        auto hosts = r_networking::r_ipv4_subnet_hosts("192.168.68.69", "255.255.252.0", 1000);
        RTF_ASSERT(hosts.empty());
    }

    // /30 -> 2 usable hosts.
    {
        auto hosts = r_networking::r_ipv4_subnet_hosts("10.0.0.1", "255.255.255.252", 4096);
        RTF_ASSERT(hosts.size() == 2);
        RTF_ASSERT(hosts.front() == "10.0.0.1");
        RTF_ASSERT(hosts.back() == "10.0.0.2");
    }

    // Degenerate / invalid inputs -> empty.
    {
        RTF_ASSERT(r_networking::r_ipv4_subnet_hosts("10.0.0.1", "255.255.255.255", 4096).empty()); // /32
        RTF_ASSERT(r_networking::r_ipv4_subnet_hosts("10.0.0.1", "0.0.0.0", 4096).empty());         // /0 rejected
        RTF_ASSERT(r_networking::r_ipv4_subnet_hosts("10.0.0.1", "255.0.255.0", 4096).empty());     // non-contiguous
        RTF_ASSERT(r_networking::r_ipv4_subnet_hosts("not-an-ip", "255.255.255.0", 4096).empty());
        RTF_ASSERT(r_networking::r_ipv4_subnet_hosts("10.0.0.1", "", 4096).empty());
    }
}

void test_r_utils::test_qr_code_encode()
{
    // A cloud activation link, the kind of text the status panel shows.
    auto qr = r_qr_code::encode("https://app.revere.video/activate?user_code=ABCD-EFGH");
    RTF_ASSERT(qr.size >= 21 && (qr.size - 17) % 4 == 0);
    RTF_ASSERT(qr.modules.size() == (size_t)qr.size * qr.size);

    // Finder patterns (7x7: dark ring, light ring, dark 3x3 core) in three
    // corners, with a light separator; none in the bottom-right.
    auto finder_at = [&](int ox, int oy) {
        for(int y = 0; y < 7; ++y)
        {
            for(int x = 0; x < 7; ++x)
            {
                bool ring = x == 0 || y == 0 || x == 6 || y == 6;
                bool core = x >= 2 && x <= 4 && y >= 2 && y <= 4;
                if(qr.dark(ox + x, oy + y) != (ring || core))
                    return false;
            }
        }
        return true;
    };
    RTF_ASSERT(finder_at(0, 0));
    RTF_ASSERT(finder_at(qr.size - 7, 0));
    RTF_ASSERT(finder_at(0, qr.size - 7));
    RTF_ASSERT(!finder_at(qr.size - 7, qr.size - 7));
    for(int i = 0; i < 8; ++i)
        RTF_ASSERT(!qr.dark(7, i) && !qr.dark(i, 7));

    // Same text, same code.
    auto again = r_qr_code::encode("https://app.revere.video/activate?user_code=ABCD-EFGH");
    RTF_ASSERT(again.size == qr.size && again.modules == qr.modules);

    // Longer text needs a bigger code.
    RTF_ASSERT(r_qr_code::encode(string(200, 'a')).size > qr.size);

    // Too long for any QR version: empty, not a throw.
    auto too_long = r_qr_code::encode(string(5000, 'a'));
    RTF_ASSERT(too_long.size == 0 && too_long.modules.empty());
}

#ifdef WIN32
#pragma warning(pop)
#endif

static bool _wait_until(std::function<bool()> pred, int millis = 3000)
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

void test_r_utils::test_socket_peer_closed()
{
    int port = RTF_NEXT_PORT();

    r_socket server_sok;
    server_sok.bind( port, "127.0.0.1" );
    server_sok.listen();

    r_socket client;
    client.connect( "127.0.0.1", port );
    auto conn = server_sok.accept();

    // Connected and quiet: not closed.
    RTF_ASSERT( !conn.peer_closed() );

    // Unread data waiting: still not closed, and the data is left in place.
    uint32_t val = 41;
    client.send( &val, 4 );
    RTF_ASSERT( _wait_until([&](){ uint64_t ms = 0; return conn.wait_till_recv_wont_block(ms); }) );
    RTF_ASSERT( !conn.peer_closed() );
    uint32_t got = 0;
    RTF_ASSERT( conn.recv( &got, 4 ) == 4 );
    RTF_ASSERT( got == 41 );

    // The client hangs up: closed.
    client.close();
    RTF_ASSERT( _wait_until([&](){ return conn.peer_closed(); }) );

    // A socket we've closed ourselves counts as closed too.
    conn.close();
    RTF_ASSERT( conn.peer_closed() );
}

// At most `workers` handlers run at once, up to `maxPending` connections wait
// for a worker, and the rest go to the overload callback and are closed. The
// queued ones are served once workers free up.
void test_r_utils::test_server_threaded_bounded_pool()
{
    int port = RTF_NEXT_PORT();

    std::mutex m;
    std::condition_variable cv;
    bool release = false;
    std::atomic<int> running {0}, max_running {0}, handled {0}, overloaded {0};

    r_server_threaded<r_socket> server( port, [&](r_socket& conn){
        int now = ++running;
        int prev = max_running.load();
        while(now > prev && !max_running.compare_exchange_weak(prev, now)) {}
        {
            std::unique_lock<std::mutex> g(m);
            cv.wait(g, [&](){ return release; });
        }
        uint8_t ok = 1;
        conn.send( &ok, 1 );
        --running;
        ++handled;
    }, "127.0.0.1", 2, 2 );
    server.set_overload_cb( [&](r_socket&){ ++overloaded; } );

    thread th( &r_server_threaded<r_socket>::start, &server );
    RTF_ASSERT( _wait_until([&](){ return server.started(); }) );

    std::vector<r_socket> clients(6);

    // Two connections occupy both workers.
    clients[0].connect( "127.0.0.1", port );
    clients[1].connect( "127.0.0.1", port );
    RTF_ASSERT( _wait_until([&](){ return running == 2; }) );

    // Two more wait in the queue: nothing refused, nothing extra running.
    clients[2].connect( "127.0.0.1", port );
    clients[3].connect( "127.0.0.1", port );
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    RTF_ASSERT( overloaded == 0 );
    RTF_ASSERT( running == 2 );

    // The queue is full: the next two are refused and closed.
    clients[4].connect( "127.0.0.1", port );
    clients[5].connect( "127.0.0.1", port );
    RTF_ASSERT( _wait_until([&](){ return overloaded == 2; }) );
    RTF_ASSERT( _wait_until([&](){ uint64_t ms = 0; return clients[4].wait_till_recv_wont_block(ms) && clients[4].peer_closed(); }) );

    // Release the workers: the queued two are served after them.
    {
        std::lock_guard<std::mutex> g(m);
        release = true;
    }
    cv.notify_all();
    RTF_ASSERT( _wait_until([&](){ return handled == 4; }) );
    RTF_ASSERT( max_running == 2 );
    for(int i = 0; i < 4; ++i)
    {
        uint8_t ok = 0;
        RTF_ASSERT( clients[i].recv( &ok, 1 ) == 1 && ok == 1 );
    }

    server.stop();
    th.join();
    server.join_all();
    RTF_ASSERT( handled == 4 );
}

static void _remove_instance_files(const string& path)
{
    for(const auto& p : {path, path + ".notify"})
        if(r_fs::file_exists(p))
            r_fs::remove_file(p);
}

void test_r_utils::test_instance_lock_one_holder()
{
    const string path = "instance_lock_test.lock";
    _remove_instance_files(path);
    {
        r_instance_lock first(path);
        RTF_ASSERT(first.held());

        // A second claim while the first is alive fails, and learns the
        // holder's pid (ours here: same process, separate lock).
        r_instance_lock second(path);
        RTF_ASSERT(!second.held());
#ifdef IS_WINDOWS
        RTF_ASSERT_EQUAL(second.holder_pid(), (int64_t)GetCurrentProcessId());
#else
        RTF_ASSERT_EQUAL(second.holder_pid(), (int64_t)getpid());
#endif
    }
    // Released with the holder.
    r_instance_lock third(path);
    RTF_ASSERT(third.held());
    _remove_instance_files(path);
}

void test_r_utils::test_instance_lock_stale_file()
{
    // A lock file left by a crashed holder (or naming some other process) is
    // not a running instance: nothing holds the OS lock.
    const string path = "instance_lock_stale.lock";
    _remove_instance_files(path);
    string stale = "999999";
    r_fs::write_file((const uint8_t*)stale.c_str(), stale.size(), path);
    r_instance_lock l(path);
    RTF_ASSERT(l.held());
    _remove_instance_files(path);
}

void test_r_utils::test_instance_lock_notify()
{
    const string path = "instance_lock_notify.lock";
    _remove_instance_files(path);

    // A request from before the holder started isn't for it.
    r_instance_lock::notify(path);
    r_instance_lock holder(path);
    RTF_ASSERT(holder.held());
    RTF_ASSERT(!holder.notified());

    // Each request is seen once.
    r_instance_lock::notify(path);
    RTF_ASSERT(holder.notified());
    RTF_ASSERT(!holder.notified());
    r_instance_lock::notify(path);
    r_instance_lock::notify(path);
    RTF_ASSERT(holder.notified());
    RTF_ASSERT(!holder.notified());

    // Only the holder hears them.
    r_instance_lock other(path);
    r_instance_lock::notify(path);
    RTF_ASSERT(!other.notified());
    _remove_instance_files(path);
}

// The Exec= of the Linux autostart entry. Run from an AppImage, the executable
// is inside its temporary mount, which is gone after a reboot: the entry must
// launch the AppImage file ($APPIMAGE) instead.
void test_r_utils::test_linux_autostart_exec()
{
    using r_utils::r_startup::linux_launch_env;
    using r_utils::r_startup::linux_autostart_exec;
    const string mounted = "/tmp/.mount_Revere1a2B3c/usr/bin/revere";

    linux_launch_env appimage;
    appimage.appimage = "/home/pat/Apps/Revere-x86_64.AppImage";
    RTF_ASSERT(linux_autostart_exec(appimage, mounted, "--start_minimized") == "/home/pat/Apps/Revere-x86_64.AppImage --start_minimized");

    // Quoted for the Exec key when the path needs it.
    appimage.appimage = "/home/pat/My Apps/Revere.AppImage";
    RTF_ASSERT(linux_autostart_exec(appimage, mounted, "--start_minimized") == "\"/home/pat/My Apps/Revere.AppImage\" --start_minimized");
    appimage.appimage = "/home/pat/a$b\"c";
    RTF_ASSERT(linux_autostart_exec(appimage, mounted, "") == R"("/home/pat/a\\$b\\"c")");

    // A .deb or other install: the executable's own path.
    RTF_ASSERT(linux_autostart_exec(linux_launch_env(), "/usr/bin/revere", "--start_minimized") == "/usr/bin/revere --start_minimized");
    RTF_ASSERT(linux_autostart_exec(linux_launch_env(), "/usr/bin/revere", "") == "/usr/bin/revere");

    // Snap: its stable command, whatever else is set.
    linux_launch_env snap;
    snap.snap = "/snap/revere/12";
    snap.snap_name = "revere";
    snap.appimage = "/home/pat/Apps/Revere-x86_64.AppImage";
    RTF_ASSERT(linux_autostart_exec(snap, "/snap/revere/12/bin/revere", "--start_minimized") == "/snap/bin/revere --start_minimized");
    snap.snap_name.clear();
    RTF_ASSERT(linux_autostart_exec(snap, "/snap/revere/12/bin/revere", "") == "/snap/bin/revere");

    // Flatpak: its app id, as before.
    linux_launch_env flatpak;
    flatpak.flatpak_id = "io.github.dicroce.Revere";
    RTF_ASSERT(linux_autostart_exec(flatpak, mounted, "--start_minimized") == "io.github.dicroce.Revere --start_minimized");

    // A literal % is %%, quoted or not.
    appimage.appimage = "/home/pat/100%/Revere.AppImage";
    RTF_ASSERT(linux_autostart_exec(appimage, mounted, "") == "/home/pat/100%%/Revere.AppImage");
    appimage.appimage = "/home/pat/100% sure/Revere.AppImage";
    RTF_ASSERT(linux_autostart_exec(appimage, mounted, "") == "\"/home/pat/100%% sure/Revere.AppImage\"");

    // At startup an existing entry is rewritten when its Exec= isn't today's:
    // one written by an older revere from an AppImage launches the mount.
    using r_utils::r_startup::desktop_entry_exec;
    const string old_entry =
        "[Desktop Entry]\r\nType=Application\r\nName=Revere\r\n"
        "Exec=/tmp/.mount_Revere9z8Y7x/usr/bin/revere --start_minimized\r\nIcon=revere\r\n";
    RTF_ASSERT(desktop_entry_exec(old_entry) == "/tmp/.mount_Revere9z8Y7x/usr/bin/revere --start_minimized");
    appimage.appimage = "/home/pat/Apps/Revere-x86_64.AppImage";
    const auto want = linux_autostart_exec(appimage, mounted, "--start_minimized");
    RTF_ASSERT(desktop_entry_exec(old_entry) != want);
    const string current_entry = "[Desktop Entry]\nType=Application\nName=Revere\nExec=" + want + "\nIcon=revere\n";
    RTF_ASSERT(desktop_entry_exec(current_entry) == want);
    RTF_ASSERT(desktop_entry_exec("[Desktop Entry]\nName=Revere\n").empty());
    RTF_ASSERT(desktop_entry_exec("Exec=/usr/bin/revere") == "/usr/bin/revere");

#ifndef IS_LINUX
    RTF_ASSERT(!r_utils::r_startup::refresh_autostart("Revere", mounted, "--start_minimized"));
#endif
}

// ---------------------------------------------------------------------------
// r_ssl_server_socket
// ---------------------------------------------------------------------------


void test_r_utils::test_ssl_server_config_generate()
{
    auto pems = r_ssl_server_config::generate_self_signed("revere-test", 30);
    RTF_ASSERT(r_string_utils::contains(pems.first, "-----BEGIN CERTIFICATE-----"));
    RTF_ASSERT(r_string_utils::contains(pems.second, "PRIVATE KEY-----"));

    auto cfg = make_shared<r_ssl_server_config>(pems.first, pems.second);
    RTF_ASSERT(cfg->cert_pem() == pems.first);

    // 32 bytes as "AB:CD:...": 32*2 hex chars + 31 colons.
    auto fp = cfg->fingerprint_sha256();
    RTF_ASSERT(fp.size() == 95);
    RTF_ASSERT(fp[2] == ':');
    for(size_t i = 0; i < fp.size(); ++i)
    {
        if(i % 3 == 2)
            RTF_ASSERT(fp[i] == ':');
        else RTF_ASSERT(isxdigit((unsigned char)fp[i]));
    }

    // Same PEM → same fingerprint; a fresh cert → a different one.
    auto cfg2 = make_shared<r_ssl_server_config>(pems.first, pems.second);
    RTF_ASSERT(cfg2->fingerprint_sha256() == fp);
    auto other = r_ssl_server_config::generate_self_signed("revere-test", 30);
    r_ssl_server_config cfg3(other.first, other.second);
    RTF_ASSERT(cfg3.fingerprint_sha256() != fp);
}

void test_r_utils::test_ssl_server_config_rejects_mismatched_key()
{
    auto a = r_ssl_server_config::generate_self_signed("a", 30);
    auto b = r_ssl_server_config::generate_self_signed("b", 30);
    RTF_ASSERT_THROWS(r_ssl_server_config(a.first, b.second), r_exception);
    RTF_ASSERT_THROWS(r_ssl_server_config("not a cert", a.second), r_exception);
    RTF_ASSERT_THROWS(r_ssl_server_config(a.first, "not a key"), r_exception);
}

void test_r_utils::test_ssl_server_socket_roundtrip()
{
    auto pems = r_ssl_server_config::generate_self_signed();
    auto cfg = make_shared<r_ssl_server_config>(pems.first, pems.second);

    int port = RTF_NEXT_PORT();

    r_ssl_server_socket listener;
    // bind() without a config must fail loudly.
    RTF_ASSERT_THROWS(listener.bind(port, "127.0.0.1"), r_exception);
    listener.set_config(cfg);
    listener.bind(port, "127.0.0.1");
    listener.listen();

    string server_error;
    thread t([&](){
        try
        {
            auto conn = listener.accept();
            RTF_ASSERT(!conn.handshake_done());   // deferred until first I/O
            RTF_ASSERT(conn.valid());

            // Accepted sockets are movable (r_server_threaded queues them).
            r_ssl_server_socket moved(std::move(conn));
            RTF_ASSERT(!conn.valid());

            uint32_t val = 0;
            uint64_t ms = 5000;
            RTF_ASSERT(moved.wait_till_recv_wont_block(ms));
            RTF_ASSERT(moved.handshake_done());
            r_networking::r_recv(moved, &val, 4);
            RTF_ASSERT(moved.get_peer_ip() == "127.0.0.1");
            val += 1;
            r_networking::r_send(moved, &val, 4);
        }
        catch(const std::exception& ex)
        {
            server_error = ex.what();
        }
    });

    r_ssl_socket client(false);   // encrypted, unauthenticated: what vision will do
    client.connect("127.0.0.1", port);
    RTF_ASSERT(client.valid());

    uint32_t val = 41;
    r_networking::r_send(client, &val, 4);
    r_networking::r_recv(client, &val, 4);
    RTF_ASSERT(val == 42);

    client.close();
    t.join();
    RTF_ASSERT(server_error.empty());
}

void test_r_utils::test_ssl_server_socket_rejects_plaintext_client()
{
    auto pems = r_ssl_server_config::generate_self_signed();
    auto cfg = make_shared<r_ssl_server_config>(pems.first, pems.second);

    int port = RTF_NEXT_PORT();

    r_ssl_server_socket listener(cfg);
    listener.bind(port, "127.0.0.1");
    listener.listen();

    int server_rc = 0;
    bool server_valid_after = true;
    thread t([&](){
        auto conn = listener.accept();
        char buf[64];
        server_rc = conn.recv(buf, sizeof(buf));
        server_valid_after = conn.valid();
    });

    r_socket client;
    client.connect("127.0.0.1", port);
    string junk = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    client.send(junk.c_str(), junk.size());

    t.join();
    RTF_ASSERT(server_rc < 0);
    RTF_ASSERT(!server_valid_after);
}

void test_r_utils::test_ssl_server_socket_threaded_server()
{
    auto pems = r_ssl_server_config::generate_self_signed();
    auto cfg = make_shared<r_ssl_server_config>(pems.first, pems.second);

    int port = RTF_NEXT_PORT();

    atomic<int> served {0};
    r_server_threaded<r_ssl_server_socket> server(port, [&](r_ssl_server_socket& conn){
        uint32_t val = 0;
        r_networking::r_recv(conn, &val, 4);
        val *= 2;
        r_networking::r_send(conn, &val, 4);
        ++served;
    }, "127.0.0.1", 4);

    // The template default-constructs its listener; the config goes in here.
    server.get_socket().set_config(cfg);

    thread st(&r_server_threaded<r_ssl_server_socket>::start, &server);
    while(!server.started())
        this_thread::sleep_for(chrono::milliseconds(10));

    // Several clients at once: concurrent handshakes on the worker pool.
    const int N = 6;
    vector<thread> clients;
    atomic<int> ok {0};
    mutex errors_lock;
    vector<string> errors;
    for(int i = 0; i < N; ++i)
    {
        clients.emplace_back([&, i](){
            try
            {
                r_ssl_socket c(false);
                c.connect("127.0.0.1", port);
                uint32_t v = (uint32_t)(i + 1);
                r_networking::r_send(c, &v, 4);
                int got = r_networking::r_recv(c, &v, 4);
                if(got == 4 && v == (uint32_t)((i + 1) * 2))
                    ++ok;
                else
                {
                    lock_guard<mutex> g(errors_lock);
                    errors.push_back(r_string_utils::format("client %d: got %d bytes, value %u", i, got, v));
                }
            }
            catch(const std::exception& ex)
            {
                lock_guard<mutex> g(errors_lock);
                errors.push_back(r_string_utils::format("client %d: %s", i, ex.what()));
            }
        });
    }
    for(auto& c : clients)
        c.join();

    // stop() wakes accept() with a plain TCP connect; that must not hang.
    // Tear down before asserting: a joinable thread outliving a failed assert
    // would std::terminate() and hide the message.
    server.stop();
    st.join();
    server.join_all();

    for(auto& e : errors)
        printf("  %s\n", e.c_str());
    RTF_ASSERT(errors.empty());
    RTF_ASSERT(ok == N);
    RTF_ASSERT(served == N);
}


// ---------------------------------------------------------------------------
// r_local_api_token
// ---------------------------------------------------------------------------

void test_r_utils::test_local_api_token_roundtrip()
{
    auto dir = r_fs::temp_file_name(r_fs::working_directory(), "local_token_ut_");
    r_fs::mkdir_p(dir);

    // Nothing there yet.
    RTF_ASSERT(r_local_api_token::read(dir).is_null());
    RTF_ASSERT(r_local_api_token::read("").is_null());

    r_local_api_token::write(dir, "first-token");
    RTF_ASSERT(r_local_api_token::read(dir).value() == "first-token");

    // Rewritten in place (a Revere restart), surrounding whitespace ignored.
    r_local_api_token::write(dir, "  second-token\n");
    RTF_ASSERT(r_local_api_token::read(dir).value() == "second-token");

#ifndef IS_WINDOWS
    // Owner-only.
    struct stat st;
    RTF_ASSERT(::stat(r_local_api_token::path(dir).c_str(), &st) == 0);
    RTF_ASSERT((st.st_mode & 0777) == 0600);
#endif

    r_local_api_token::remove(dir);
    RTF_ASSERT(r_local_api_token::read(dir).is_null());
    // Removing what isn't there is fine.
    r_local_api_token::remove(dir);

    // An empty file isn't a token.
    r_fs::atomic_write_private_file((const uint8_t*)"", 0, r_local_api_token::path(dir));
    RTF_ASSERT(r_local_api_token::read(dir).is_null());
    r_local_api_token::remove(dir);

    r_fs::rmdir(dir);

    // Where Revere keeps it by default is knowable.
    auto top = r_local_api_token::default_top_dir();
    RTF_ASSERT(!top.empty());
    RTF_ASSERT(r_string_utils::contains(r_string_utils::to_lower(top), "revere"));
}

void test_r_utils::test_local_api_token_loopback_hosts()
{
    RTF_ASSERT(r_local_api_token::is_loopback_host("127.0.0.1"));
    RTF_ASSERT(r_local_api_token::is_loopback_host("127.1.2.3"));
    RTF_ASSERT(r_local_api_token::is_loopback_host("localhost"));
    RTF_ASSERT(r_local_api_token::is_loopback_host("LocalHost"));
    RTF_ASSERT(r_local_api_token::is_loopback_host("::1"));
    RTF_ASSERT(r_local_api_token::is_loopback_host("[::1]"));
    RTF_ASSERT(!r_local_api_token::is_loopback_host("192.168.1.10"));
    RTF_ASSERT(!r_local_api_token::is_loopback_host("10.127.0.1"));
    RTF_ASSERT(!r_local_api_token::is_loopback_host("localhost.evil.com"));
    RTF_ASSERT(!r_local_api_token::is_loopback_host(""));

    RTF_ASSERT(r_local_api_token::bearer("abc") == "Bearer abc");
}
