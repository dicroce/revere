#include "test_r_mail_rings.h"
#include "r_vss/r_mail_rings.h"
#include "r_utils/r_ssl_server_socket.h"
#include "r_utils/r_ssl_socket.h"
#include "r_utils/r_string_utils.h"
#include <chrono>
#include <ctime>
#include <mutex>
#include <thread>
#include <vector>

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

REGISTER_TEST_FIXTURE(test_r_mail_rings);

namespace
{

const char* USER = "rv-ring-7f3a";
const char* PASS = "p4ss-w0rd-9c1e";

// The doorbell's subject time for ms since the epoch, in this box's time zone, as Reolink writes it.
string subject_at(int64_t ms)
{
    time_t secs = (time_t)(ms / 1000);
    tm t {};
#ifdef IS_WINDOWS
    localtime_s(&t, &secs);
#else
    localtime_r(&secs, &t);
#endif
    char buf[64];
    strftime(buf, sizeof(buf), "Someone Rang the Doorbell at %Y/%m/%d %H:%M:%S", &t);
    return buf;
}

string ring_mail(const string& subject)
{
    return "From: doorbell@example.com\r\nTo: rings@revere.local\r\nSubject: " + subject +
           "\r\nContent-Type: text/plain\r\n\r\nAlarm Event: Visitor\r\nAlarm Camera Name: Front Door\r\n";
}

struct rig
{
    mutex lock;
    vector<r_mail_ring> rings;
    int64_t now_ms {1764018345000};   // 2026-11-24, a fixed arrival time for the press-time checks
    string doorbell_ip {"127.0.0.1"};
    unique_ptr<r_mail_rings> server;
    int port;

    explicit rig(int p, function<void(r_mail_rings_config&)> tweak = {}) : port(p)
    {
        static auto pems = r_ssl_server_config::generate_self_signed("revere-mail-test", 30);
        r_mail_rings_config c;
        c.bind_ip = "127.0.0.1";
        c.port = port;
        c.user = USER;
        c.password = PASS;
        c.cert_pem = pems.first;
        c.key_pem = pems.second;
        if (tweak)
            tweak(c);
        server = make_unique<r_mail_rings>(c,
            [this](const string& ip) { return ip == doorbell_ip ? r_nullable<string>(string("door-1")) : r_nullable<string>(); },
            [this](const r_mail_ring& r) { lock_guard<mutex> g(lock); rings.push_back(r); },
            [](const string&) { return (int64_t)0; },
            [this]() { return now_ms; });
        server->start();
        // Until it answers.
        for (int i = 0; i < 200; ++i)
        {
            try { r_socket probe; probe.connect("127.0.0.1", port); probe.close(); break; }
            catch (...) { this_thread::sleep_for(milliseconds(10)); }
        }
        this_thread::sleep_for(milliseconds(50));
    }
    ~rig() { server->stop(); }
    size_t ring_count() { lock_guard<mutex> g(lock); return rings.size(); }
};

// A mail client: plain until start_tls().
struct client
{
    r_ssl_socket sok;
    string buf;
    explicit client(int port) { sok.set_io_timeout(5000); sok.connect_plain("127.0.0.1", port); }
    string line()
    {
        for (int guard = 0; guard < 2000; ++guard)
        {
            auto nl = buf.find("\r\n");
            if (nl != string::npos)
            {
                auto l = buf.substr(0, nl);
                buf.erase(0, nl + 2);
                return l;
            }
            uint64_t wait = 3000;
            if (!sok.wait_till_recv_wont_block(wait))
                return "(timeout)";
            char chunk[2048];
            int n = sok.recv(chunk, sizeof(chunk));
            if (n <= 0)
                return "(closed)";
            buf.append(chunk, (size_t)n);
        }
        return "(junk)";
    }
    // The last line of a reply (multi-line ones end with "NNN text").
    string reply()
    {
        for (;;)
        {
            auto l = line();
            if (l.size() < 4 || l[3] != '-')
                return l;
        }
    }
    void send(const string& s) { string out = s + "\r\n"; sok.send(out.data(), out.size()); }
    string say(const string& s) { send(s); return reply(); }
    static bool code(const string& reply, const char* c) { return reply.compare(0, 3, c) == 0; }
    // Greeting, EHLO, STARTTLS, EHLO again over TLS.
    void secure()
    {
        RTF_ASSERT(code(reply(), "220"));
        RTF_ASSERT(code(say("EHLO doorbell"), "250"));
        RTF_ASSERT(code(say("STARTTLS"), "220"));
        sok.start_tls();
        RTF_ASSERT(code(say("EHLO doorbell"), "250"));
    }
    string auth_plain(const string& user, const string& pass)
    {
        string creds = string(1, '\0') + user + string(1, '\0') + pass;
        return say("AUTH PLAIN " + r_string_utils::to_base64(creds.data(), creds.size()));
    }
    // MAIL, RCPT, DATA and the message; the reply to its end.
    string mail(const string& message)
    {
        if (!code(say("MAIL FROM:<doorbell@example.com>"), "250")) return "MAIL refused";
        if (!code(say("RCPT TO:<rings@revere.local>"), "250")) return "RCPT refused";
        if (!code(say("DATA"), "354")) return "DATA refused";
        sok.send(message.data(), message.size());
        return say(".");
    }
};

}

void test_r_mail_rings::test_reading_a_ring_mail()
{
    string subject, text;
    read_mail(ring_mail("Someone Rang the Doorbell at 2026/11/24 13:05:45"), subject, text);
    RTF_ASSERT_EQUAL(subject, string("Someone Rang the Doorbell at 2026/11/24 13:05:45"));
    RTF_ASSERT(is_ring_mail(subject, text));
    // A motion alert isn't a ring.
    read_mail("Subject: Motion Alert from Front Door\r\n\r\nAlarm Event: Motion Detection\r\n", subject, text);
    RTF_ASSERT(!is_ring_mail(subject, text));
    // An encoded subject, and a base64 body: read the same.
    read_mail("Subject: =?UTF-8?B?U29tZW9uZSBSYW5nIHRoZSBEb29yYmVsbA==?= at 2026/11/24 13:05:45\r\n\r\nx\r\n", subject, text);
    RTF_ASSERT(is_ring_mail(subject, text));
    string b64 = r_string_utils::to_base64("Alarm Event: Visitor", 20);
    read_mail("Subject: alert\r\nContent-Transfer-Encoding: base64\r\n\r\n" + b64 + "\r\n", subject, text);
    RTF_ASSERT(is_ring_mail(subject, text));
    read_mail("Subject: alert\r\nContent-Type: multipart/mixed; boundary=b\r\n\r\n--b\r\nContent-Type: text/plain\r\nContent-Transfer-Encoding: base64\r\n\r\n" +
              b64 + "\r\n--b--\r\n", subject, text);
    RTF_ASSERT(is_ring_mail(subject, text));
    // A folded subject is joined.
    read_mail("Subject: Someone Rang\r\n the Doorbell at 2026/11/24 13:05:45\r\n\r\n", subject, text);
    RTF_ASSERT(is_ring_mail(subject, text));
}

void test_r_mail_rings::test_the_press_time()
{
    const int64_t arrival = 1764018345000;
    bool read = false;
    // 14 s before it arrived, as the real ones are: believed.
    auto at = ring_subject_time_ms(subject_at(arrival - 14000));
    RTF_ASSERT(!at.is_null());
    RTF_ASSERT_EQUAL(ring_press_ms(at, 0, arrival, read), arrival - 14000);
    RTF_ASSERT(read);
    // The camera's clock 3 s fast: corrected.
    RTF_ASSERT_EQUAL(ring_press_ms(ring_subject_time_ms(subject_at(arrival - 11000)), 3000, arrival, read), arrival - 14000);
    RTF_ASSERT(read);
    // An hour off (a wrong clock or zone): the arrival less 15 s, and said so.
    RTF_ASSERT_EQUAL(ring_press_ms(ring_subject_time_ms(subject_at(arrival - 3600000)), 0, arrival, read), arrival - 15000);
    RTF_ASSERT(!read);
    // In the future by more than 5 s: the same.
    RTF_ASSERT_EQUAL(ring_press_ms(ring_subject_time_ms(subject_at(arrival + 10000)), 0, arrival, read), arrival - 15000);
    RTF_ASSERT(!read);
    // No time in the subject.
    RTF_ASSERT(ring_subject_time_ms("Someone Rang the Doorbell").is_null());
    RTF_ASSERT_EQUAL(ring_press_ms(r_nullable<int64_t>(), 0, arrival, read), arrival - 15000);
}

void test_r_mail_rings::test_a_ring_over_starttls_with_auth_plain()
{
    rig r(12531);
    client c(r.port);
    c.secure();
    RTF_ASSERT(client::code(c.auth_plain(USER, PASS), "235"));
    RTF_ASSERT(client::code(c.mail(ring_mail(subject_at(r.now_ms - 13000))), "250"));
    RTF_ASSERT(client::code(c.say("QUIT"), "221"));
    RTF_ASSERT_EQUAL(r.ring_count(), (size_t)1);
    {
        lock_guard<mutex> g(r.lock);
        RTF_ASSERT_EQUAL(r.rings[0].camera_id, string("door-1"));
        RTF_ASSERT_EQUAL(r.rings[0].press_ms, r.now_ms - 13000);
        RTF_ASSERT_EQUAL(r.rings[0].arrival_ms, r.now_ms);
        RTF_ASSERT(r.rings[0].clock_read);
    }
    auto s = r.server->status();
    RTF_ASSERT_EQUAL(s.rings, (uint64_t)1);
    RTF_ASSERT_EQUAL(s.last_ring_ms, r.now_ms - 13000);
}

void test_r_mail_rings::test_auth_login_and_a_motion_mail()
{
    rig r(12532);
    client c(r.port);
    c.secure();
    RTF_ASSERT(client::code(c.say("AUTH LOGIN"), "334"));
    RTF_ASSERT(client::code(c.say(r_string_utils::to_base64(USER, strlen(USER))), "334"));
    RTF_ASSERT(client::code(c.say(r_string_utils::to_base64(PASS, strlen(PASS))), "235"));
    // A motion alert: taken, counted, no ring.
    RTF_ASSERT(client::code(c.mail("Subject: Motion Alert\r\n\r\nAlarm Event: Motion Detection\r\n"), "250"));
    // And a ring after it, on the same connection.
    RTF_ASSERT(client::code(c.mail(ring_mail(subject_at(r.now_ms - 12000))), "250"));
    RTF_ASSERT_EQUAL(r.ring_count(), (size_t)1);
    RTF_ASSERT_EQUAL(r.server->status().other_mail, (uint64_t)1);
}

void test_r_mail_rings::test_nothing_before_starttls()
{
    // No login and no mail in the clear: the password and the mail never cross the network unencrypted.
    rig r(12533);
    client c(r.port);
    RTF_ASSERT(client::code(c.reply(), "220"));
    RTF_ASSERT(client::code(c.say("EHLO doorbell"), "250"));
    RTF_ASSERT(client::code(c.auth_plain(USER, PASS), "530"));
    RTF_ASSERT(client::code(c.say("MAIL FROM:<doorbell@example.com>"), "530"));
    RTF_ASSERT(client::code(c.say("DATA"), "503"));
    RTF_ASSERT_EQUAL(r.ring_count(), (size_t)0);
    // Over TLS but not logged in: still no mail.
    c.send("STARTTLS");
    RTF_ASSERT(client::code(c.reply(), "220"));
    c.sok.start_tls();
    RTF_ASSERT(client::code(c.say("MAIL FROM:<doorbell@example.com>"), "530"));
}

void test_r_mail_rings::test_a_wrong_login()
{
    rig r(12534);
    client c(r.port);
    c.secure();
    RTF_ASSERT(client::code(c.auth_plain(USER, "wrong"), "535"));
    RTF_ASSERT(client::code(c.auth_plain("someone", PASS), "535"));
    RTF_ASSERT(client::code(c.say("MAIL FROM:<doorbell@example.com>"), "530"));
    // A third wrong one ends the connection.
    RTF_ASSERT(client::code(c.auth_plain(USER, "again"), "535"));
    RTF_ASSERT_EQUAL(c.line(), string("(closed)"));
    RTF_ASSERT_EQUAL(r.server->status().bad_logins, (uint64_t)3);
}

void test_r_mail_rings::test_a_stranger_is_refused()
{
    // Only from a doorbell that sends rings by email: here, the doorbell is elsewhere.
    rig r(12535);
    r.doorbell_ip = "10.9.9.9";
    client c(r.port);
    RTF_ASSERT(client::code(c.reply(), "554"));
    RTF_ASSERT_EQUAL(c.line(), string("(closed)"));
    RTF_ASSERT_EQUAL(r.server->status().refused, (uint64_t)1);
}

void test_r_mail_rings::test_a_mail_too_big()
{
    rig r(12536, [](r_mail_rings_config& c) { c.max_mail_bytes = 2000; });
    client c(r.port);
    c.secure();
    RTF_ASSERT(client::code(c.auth_plain(USER, PASS), "235"));
    string big = ring_mail(subject_at(r.now_ms - 13000)) + string(3000, 'x') + "\r\n";
    RTF_ASSERT(client::code(c.mail(big), "552"));
    RTF_ASSERT_EQUAL(r.ring_count(), (size_t)0);
    // The connection is still good for the next one.
    RTF_ASSERT(client::code(c.mail(ring_mail(subject_at(r.now_ms - 13000))), "250"));
    RTF_ASSERT_EQUAL(r.ring_count(), (size_t)1);
}

void test_r_mail_rings::test_too_many_mails_a_minute()
{
    rig r(12537, [](r_mail_rings_config& c) { c.mails_per_minute = 2; });
    client c(r.port);
    c.secure();
    RTF_ASSERT(client::code(c.auth_plain(USER, PASS), "235"));
    for (int i = 0; i < 3; ++i)
        RTF_ASSERT(client::code(c.mail(ring_mail(subject_at(r.now_ms - 13000))), "250"));
    RTF_ASSERT_EQUAL(r.ring_count(), (size_t)2);
    RTF_ASSERT_EQUAL(r.server->status().rate_limited, (uint64_t)1);
    // A minute on, they're taken again.
    r.now_ms += 61000;
    RTF_ASSERT(client::code(c.mail(ring_mail(subject_at(r.now_ms - 13000))), "250"));
    RTF_ASSERT_EQUAL(r.ring_count(), (size_t)3);
}

void test_r_mail_rings::test_a_slow_client_is_let_go()
{
    // A client that stops talking is closed once a command takes longer than the limit.
    rig r(12538, [](r_mail_rings_config& c) { c.command_timeout_ms = 400; });
    client c(r.port);
    RTF_ASSERT(client::code(c.reply(), "220"));
    auto t0 = steady_clock::now();
    RTF_ASSERT_EQUAL(c.line(), string("(closed)"));
    RTF_ASSERT(duration_cast<milliseconds>(steady_clock::now() - t0).count() < 2500);
}

void test_r_mail_rings::test_it_never_binds_every_address()
{
    for (auto ip : {"", "0.0.0.0", "::"})
    {
        r_mail_rings_config c;
        c.bind_ip = ip;
        c.user = USER;
        c.password = PASS;
        c.cert_pem = "x";
        c.key_pem = "y";
        r_mail_rings m(c, {}, {});
        bool threw = false;
        try { m.start(); } catch (...) { threw = true; }
        RTF_ASSERT(threw);
    }
}

void test_r_mail_rings::test_nothing_may_follow_starttls_in_the_clear()
{
    // Commands sent after STARTTLS before the handshake would be read as if over TLS (RFC 3207): the connection ends.
    rig r(12539);
    client c(r.port);
    RTF_ASSERT(client::code(c.reply(), "220"));
    RTF_ASSERT(client::code(c.say("EHLO doorbell"), "250"));
    c.send("STARTTLS\r\nAUTH PLAIN AHgAeA==");
    RTF_ASSERT_EQUAL(c.line(), string("(closed)"));
}

void test_r_mail_rings::test_too_many_connections_at_once()
{
    // One served, one waiting, and the next told to come back later.
    rig r(12540, [](r_mail_rings_config& c) { c.max_connections = 1; });
    client a(r.port);
    RTF_ASSERT(client::code(a.reply(), "220"));
    client b(r.port);
    this_thread::sleep_for(milliseconds(100));
    client third(r.port);
    RTF_ASSERT(client::code(third.reply(), "421"));
}
