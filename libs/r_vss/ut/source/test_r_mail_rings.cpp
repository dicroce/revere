#include "test_r_mail_rings.h"
#include "r_vss/r_mail_rings.h"
#include "r_utils/r_ssl_server_socket.h"
#include "r_utils/r_ssl_socket.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_file.h"
#include <sys/stat.h>
#include <chrono>
#include <ctime>
#include <mutex>
#include <thread>
#include <vector>
#ifndef IS_WINDOWS
#include <pthread.h>
#endif

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

namespace
{

// Runs f on a thread with a small stack (256 KB, less than any platform's default), so a parser that recurses per
// character fails here, not in the field.
void on_small_stack(function<void()> f)
{
#ifdef IS_WINDOWS
    thread(f).join();
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    pthread_t t;
    auto run = [](void* p) -> void* { (*static_cast<function<void()>*>(p))(); return nullptr; };
    RTF_ASSERT(pthread_create(&t, &attr, run, &f) == 0);
    pthread_join(t, nullptr);
    pthread_attr_destroy(&attr);
#endif
}

string b64_lines(const string& raw)
{
    auto b = r_string_utils::to_base64(raw.data(), raw.size());
    string out;
    for (size_t i = 0; i < b.size(); i += 76)
        out += b.substr(i, 76) + "\r\n";
    return out;
}

}

void test_r_mail_rings::test_a_big_picture_attached()
{
    // Reolink attaches the snapshot: 1.5 MB of picture, 2 MB of base64, after the ring's text part. Read without
    // recursing per character (libstdc++'s regex did, and overflowed the stack), and the picture isn't decoded as text.
    string picture(1536 * 1024, '\0');
    for (size_t i = 0; i < picture.size(); ++i)
        picture[i] = (char)((i * 131) ^ (i >> 7));
    string mail = "Subject: =?UTF-8?B?" + r_string_utils::to_base64("Someone Rang the Doorbell", 25) + "?= at 2026/11/24 13:05:45\r\n"
                  "Content-Type: multipart/mixed; boundary=\"b1\"\r\n\r\n"
                  "--b1\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n" +
                  b64_lines("Alarm Event: Visitor\r\nAlarm Camera Name: Front Door\r\n") +
                  "--b1\r\nContent-Type: image/jpeg; name=\"snap.jpg\"\r\nContent-Transfer-Encoding: base64\r\n\r\n" +
                  b64_lines(picture) + "--b1--\r\n";
    RTF_ASSERT(mail.size() > 2 * 1000 * 1000);
    string subject, text;
    on_small_stack([&]() { read_mail(mail, subject, text); });
    RTF_ASSERT(is_ring_mail(subject, text));
    RTF_ASSERT(text.find("Alarm Camera Name: Front Door") != string::npos);
    RTF_ASSERT(text.size() < mail.size() + 4096);   // the picture is there as sent, not decoded too
    RTF_ASSERT(!ring_subject_time_ms(subject).is_null());

    // A huge subject, and one long encoded word: read the same way.
    string words = "Subject: =?UTF-8?Q?" + string(200 * 1024, 'a') + "?= " + string(200 * 1024, '=') + "\r\n\r\nx\r\n";
    on_small_stack([&]() { read_mail(words, subject, text); });
    RTF_ASSERT(subject.size() <= 2000);
    // Only a text part's base64 is read: the ring's words in an attachment aren't the mail's.
    string in_attachment = "Subject: alert\r\nContent-Type: multipart/mixed; boundary=b\r\n\r\n--b\r\nContent-Type: application/octet-stream\r\n"
                           "Content-Transfer-Encoding: base64\r\n\r\n" + b64_lines("Alarm Event: Visitor") + "--b--\r\n";
    read_mail(in_attachment, subject, text);
    RTF_ASSERT(!is_ring_mail(subject, text));
}

void test_r_mail_rings::test_the_subject_time_read_by_hand()
{
    auto a = ring_subject_time_ms("Someone Rang the Doorbell at 2026/11/24 13:05:45");
    auto b = ring_subject_time_ms("Someone Rang the Doorbell at 2026/11/24 13:05:44");
    RTF_ASSERT(!a.is_null() && !b.is_null());
    RTF_ASSERT_EQUAL(a.value() - b.value(), (int64_t)1000);
    // One-digit month, day and hour.
    auto c = ring_subject_time_ms("at 2026/3/7 9:05:45");
    RTF_ASSERT(!c.is_null());
    auto d = ring_subject_time_ms("at 2026/3/7 10:05:45");
    RTF_ASSERT_EQUAL(d.value() - c.value(), (int64_t)3600000);
    // The first good one, after a number that isn't a date.
    RTF_ASSERT_EQUAL(ring_subject_time_ms("Camera 12026/11/24 1:2:3, at 2026/11/24 13:05:45").value(), a.value());
    // Not dates.
    RTF_ASSERT(ring_subject_time_ms("at 2026/11/24 13:5:45").is_null());
    RTF_ASSERT(ring_subject_time_ms("at 2026/11/24 13:05").is_null());
    RTF_ASSERT(ring_subject_time_ms("at 2026/11/245 13:05:45").is_null());
    RTF_ASSERT(ring_subject_time_ms("at 2026-11-24 13:05:45").is_null());
    RTF_ASSERT(ring_subject_time_ms("at 2026/11/24 13:05:456").is_null());
    // Two encoded words with a space between: the space isn't the text's; between a word and plain text it is.
    string subject, text;
    read_mail("Subject: =?UTF-8?B?U29tZW9uZSBSYW5n?= =?UTF-8?Q?_the_Doorbell?= at 2026/11/24 13:05:45\r\n\r\nx\r\n", subject, text);
    RTF_ASSERT_EQUAL(subject, string("Someone Rang the Doorbell at 2026/11/24 13:05:45"));
    // A multipart mail with bare LF line ends reads the same.
    string b64 = r_string_utils::to_base64("Alarm Event: Visitor", 20);
    read_mail("Subject: alert\nContent-Type: multipart/mixed; boundary=b\n\n--b\nContent-Type: text/plain\nContent-Transfer-Encoding: base64\n\n" +
              b64 + "\n--b--\n", subject, text);
    RTF_ASSERT(is_ring_mail(subject, text));
}

void test_r_mail_rings::test_a_connection_has_a_deadline()
{
    // A client that keeps talking, a NOOP at a time, is let go when the connection's time is up.
    rig r(12541, [](r_mail_rings_config& c) { c.connection_timeout_ms = 1200; });
    client c(r.port);
    RTF_ASSERT(client::code(c.reply(), "220"));
    auto t0 = steady_clock::now();
    string last;
    for (int i = 0; i < 20; ++i)
    {
        last = c.say("NOOP");
        if (!client::code(last, "250"))
            break;
        this_thread::sleep_for(milliseconds(200));
    }
    RTF_ASSERT_EQUAL(last, string("(closed)"));
    auto took = duration_cast<milliseconds>(steady_clock::now() - t0).count();
    RTF_ASSERT(took >= 1000 && took < 3000);
}

void test_r_mail_rings::test_a_data_without_end()
{
    // A DATA too big is read to its end and refused; one that runs past twice the limit is refused, and the
    // connection ended, without waiting for an end that may never come.
    rig r(12542, [](r_mail_rings_config& c) { c.max_mail_bytes = 2000; });
    client c(r.port);
    c.secure();
    RTF_ASSERT(client::code(c.auth_plain(USER, PASS), "235"));
    RTF_ASSERT(client::code(c.say("MAIL FROM:<doorbell@example.com>"), "250"));
    RTF_ASSERT(client::code(c.say("RCPT TO:<rings@revere.local>"), "250"));
    RTF_ASSERT(client::code(c.say("DATA"), "354"));
    string lines;
    for (int i = 0; i < 100; ++i)
        lines += string(98, 'x') + "\r\n";
    c.sok.send(lines.data(), lines.size());
    RTF_ASSERT(client::code(c.reply(), "552"));
    RTF_ASSERT_EQUAL(c.line(), string("(closed)"));
    RTF_ASSERT_EQUAL(r.ring_count(), (size_t)0);
}

void test_r_mail_rings::test_wrong_logins_counted_per_address()
{
    // Three wrong logins end a connection; reconnecting doesn't start the count over: past the address's limit its
    // logins are refused, the right one too, until the 10 minutes are up.
    rig r(12543, [](r_mail_rings_config& c) { c.bad_logins_per_address = 4; });
    {
        client c(r.port);
        c.secure();
        for (int i = 0; i < 3; ++i)
            RTF_ASSERT(client::code(c.auth_plain(USER, "wrong"), "535"));
        RTF_ASSERT_EQUAL(c.line(), string("(closed)"));
    }
    {
        client c(r.port);
        c.secure();
        RTF_ASSERT(client::code(c.auth_plain(USER, "wrong"), "535"));
        RTF_ASSERT(client::code(c.auth_plain(USER, PASS), "454"));
        RTF_ASSERT_EQUAL(c.line(), string("(closed)"));
    }
    RTF_ASSERT_EQUAL(r.server->status().bad_logins, (uint64_t)4);
    r.now_ms += 10 * 60 * 1000;
    {
        client c(r.port);
        c.secure();
        RTF_ASSERT(client::code(c.auth_plain(USER, PASS), "235"));
    }
}

void test_r_mail_rings::test_which_doorbells_send_by_email()
{
    r_disco::r_camera door;
    door.id = "door";
    door.kind = string("doorbell");
    door.ipv4 = string("10.0.0.9");
    door.press_source = string("reolink_email");
    r_disco::r_camera onvif_door = door;
    onvif_door.id = "onvif_door";
    onvif_door.ipv4 = string("10.0.0.10");
    onvif_door.press_source.clear();
    r_disco::r_camera cam = door;
    cam.id = "cam";
    cam.ipv4 = string("10.0.0.11");
    cam.kind = string("camera");                 // set to email, but not a doorbell
    r_disco::r_camera nowhere = door;
    nowhere.id = "nowhere";
    nowhere.ipv4.clear();
    auto m = mail_ring_doorbells({door, onvif_door, cam, nowhere});
    RTF_ASSERT_EQUAL(m.size(), (size_t)1);
    RTF_ASSERT_EQUAL(m["10.0.0.9"], string("door"));
}

void test_r_mail_rings::test_the_login_is_made_once_and_kept_private()
{
    string dir = "mail_rings_test_dir";
    string path = dir + PATH_SLASH + "login";
    if (r_fs::file_exists(path))
        r_fs::remove_file(path);
    auto a = load_or_make_mail_login(path);
    RTF_ASSERT(a.user.compare(0, 7, "revere-") == 0);
    RTF_ASSERT(a.password.size() == 20);
    auto b = load_or_make_mail_login(path);
    RTF_ASSERT(a.user == b.user && a.password == b.password);
#ifndef IS_WINDOWS
    struct stat st {};
    RTF_ASSERT(stat(path.c_str(), &st) == 0);
    RTF_ASSERT((st.st_mode & 0777) == 0600);
#endif
    // Another box's is another login.
    r_fs::remove_file(path);
    RTF_ASSERT(load_or_make_mail_login(path).password != a.password);

    string crt = dir + PATH_SLASH + "mail_rings.crt", key = dir + PATH_SLASH + "mail_rings.key";
    for (auto& f : {crt, key})
        if (r_fs::file_exists(f))
            r_fs::remove_file(f);
    auto pems = load_or_make_mail_cert(crt, key);
    RTF_ASSERT(load_or_make_mail_cert(crt, key) == pems);
#ifndef IS_WINDOWS
    RTF_ASSERT(stat(key.c_str(), &st) == 0);
    RTF_ASSERT((st.st_mode & 0777) == 0600);
#endif
    // A spoiled one is made again.
    r_fs::atomic_write_file((const uint8_t*)"junk", 4, crt);
    auto again = load_or_make_mail_cert(crt, key);
    RTF_ASSERT(again != pems);
    r_ssl_server_config ok(again.first, again.second);
}
