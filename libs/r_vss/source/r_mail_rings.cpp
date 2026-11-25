#include "r_vss/r_mail_rings.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_server_threaded.h"
#include "r_utils/r_ssl_server_socket.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_file.h"
#include "r_utils/r_std_utils.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <chrono>
#include <ctime>
#include <vector>

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

namespace
{

const size_t MAX_LINE = 1000;               // RFC 5321's line limit, with room
const int MAX_BAD_LOGINS = 3;               // per connection
const int64_t BAD_LOGIN_WINDOW_MS = 10 * 60 * 1000;   // an address's wrong logins are counted over this
const int64_t STRANGER_LOG_EVERY_MS = 10 * 60 * 1000;
// A mail is read by hand, never with std::regex over its body or headers: libstdc++'s regex recurses once per
// character it repeats over, and a doorbell's picture, base64 in the mail, overflows the stack (GCC bug 86164).
const size_t MAX_SUBJECT = 2000;            // of a Subject header, the part read
const size_t MAX_PART_B64 = 256 * 1024;     // of a text part's base64, the part decoded
const int64_t READ_BEFORE_MS = 60 * 1000;   // a subject time this long before the mail arrived is believed
const int64_t READ_AFTER_MS = 5 * 1000;     // and this long after
const int64_t USUAL_DELAY_MS = 15 * 1000;   // Reolink sends its mail about this long after the press

int64_t _wall_ms() { return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count(); }

string _lower(string s)
{
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    return s;
}

string _b64_text(const string& s)
{
    try
    {
        auto v = r_string_utils::from_base64(s);
        return string(v.begin(), v.end());
    }
    catch (...)
    {
        return string();
    }
}

bool _same(const string& a, const string& b)
{
    // Compared whole, whatever differs where, so a wrong guess takes as long as any other.
    unsigned char d = (unsigned char)(a.size() != b.size());
    for (size_t i = 0; i < max(a.size(), b.size()); ++i)
        d |= (unsigned char)((i < a.size() ? a[i] : 0) ^ (i < b.size() ? b[i] : 0));
    return d == 0;
}

// RFC 2047 encoded words in a header: =?charset?B?...?= and =?charset?Q?...?=, the text taken as UTF-8. Found by
// hand (see MAX_SUBJECT).
string _decode_words(const string& v)
{
    string out;
    size_t at = 0;
    bool after_word = false;
    for (;;)
    {
        // =?charset?X?data?=
        auto start = v.find("=?", at);
        size_t q1 = string::npos, q2 = string::npos, end = string::npos;
        while (start != string::npos)
        {
            q1 = v.find('?', start + 2);
            if (q1 != string::npos && q1 > start + 2 && q1 + 2 < v.size() && v[q1 + 2] == '?' && strchr("bBqQ", v[q1 + 1]))
            {
                q2 = q1 + 2;
                end = v.find("?=", q2 + 1);
                if (end != string::npos)
                    break;
            }
            start = v.find("=?", start + 2);
        }
        if (start == string::npos)
            break;
        string between = v.substr(at, start - at);
        // Space between two encoded words is not part of the text.
        if (!(after_word && between.find_first_not_of(" \t") == string::npos))
            out += between;
        string enc(1, v[q1 + 1]), data = v.substr(q2 + 1, end - q2 - 1);
        if (enc == "b" || enc == "B")
            out += _b64_text(data);
        else
        {
            for (size_t i = 0; i < data.size(); ++i)
            {
                if (data[i] == '_')
                    out += ' ';
                else if (data[i] == '=' && i + 2 < data.size() && isxdigit((unsigned char)data[i + 1]) && isxdigit((unsigned char)data[i + 2]))
                {
                    out += (char)stoi(data.substr(i + 1, 2), nullptr, 16);
                    i += 2;
                }
                else
                    out += data[i];
            }
        }
        at = end + 2;
        after_word = true;
    }
    return out + v.substr(at);
}

bool _starts_with_ci(const string& s, const char* prefix)
{
    size_t n = strlen(prefix);
    if (s.size() < n)
        return false;
    for (size_t i = 0; i < n; ++i)
        if (tolower((unsigned char)s[i]) != prefix[i])
            return false;
    return true;
}

// The decoded text of a mail body's base64 text parts, read line by line: a part's headers follow its boundary line
// ("--..."), and its body runs to the next. Only a text part's (or one that doesn't say) base64 is decoded, at most
// MAX_PART_B64 of it: a picture or other attachment is skipped.
string _b64_text_parts(const string& body)
{
    string out, part;
    bool in_head = false, b64 = false, text = true;
    auto finish = [&]() {
        if (b64 && text && !part.empty())
            out += "\n" + _b64_text(part);
        part.clear();
    };
    size_t at = 0;
    while (at < body.size())
    {
        auto nl = body.find('\n', at);
        size_t len = (nl == string::npos ? body.size() : nl) - at;
        if (len > 0 && body[at + len - 1] == '\r')
            --len;
        if (len >= 2 && body[at] == '-' && body[at + 1] == '-')
        {
            finish();
            in_head = true;
            b64 = false;
            text = true;
        }
        else if (in_head)
        {
            string line = body.substr(at, min(len, MAX_LINE));
            if (line.empty())
                in_head = false;
            else if (_starts_with_ci(line, "content-transfer-encoding:"))
                b64 = _lower(line).find("base64") != string::npos;
            else if (_starts_with_ci(line, "content-type:"))
            {
                auto v = line.find_first_not_of(" \t", strlen("content-type:"));
                text = v != string::npos && _starts_with_ci(line.substr(v), "text/");
            }
        }
        else if (b64 && text && part.size() < MAX_PART_B64)
            part.append(body, at, min(len, MAX_PART_B64 - part.size()));
        if (nl == string::npos)
            break;
        at = nl + 1;
    }
    finish();
    return out;
}

}

void r_vss::read_mail(const string& message, string& subject, string& text)
{
    subject.clear();
    text.clear();
    auto split = message.find("\r\n\r\n");
    size_t skip = 4;
    if (split == string::npos)
    {
        split = message.find("\n\n");
        skip = 2;
    }
    string head = message.substr(0, split == string::npos ? message.size() : split);
    string body = split == string::npos ? string() : message.substr(split + skip);

    // Headers, folded lines joined.
    map<string, string> headers;
    string name, value;
    auto flush = [&]() { if (!name.empty()) headers[_lower(name)] = value; name.clear(); value.clear(); };
    size_t at = 0;
    while (at <= head.size())
    {
        auto nl = head.find('\n', at);
        string line = head.substr(at, nl == string::npos ? string::npos : nl - at);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty() && (line[0] == ' ' || line[0] == '\t'))
            value += " " + line.substr(line.find_first_not_of(" \t"));
        else
        {
            flush();
            auto colon = line.find(':');
            if (colon != string::npos)
            {
                name = line.substr(0, colon);
                auto v = line.find_first_not_of(" \t", colon + 1);
                value = v == string::npos ? string() : line.substr(v);
            }
        }
        if (nl == string::npos)
            break;
        at = nl + 1;
    }
    flush();

    if (headers.count("subject"))
        subject = _decode_words(headers["subject"].substr(0, MAX_SUBJECT));
    // The body as sent, and decoded when it's base64 (top level, or a text part's); a ring's words are found in either.
    text = body;
    auto type = _lower(headers["content-type"]);
    if (_lower(headers["content-transfer-encoding"]).find("base64") != string::npos && (type.empty() || _starts_with_ci(type, "text/")))
        text += "\n" + _b64_text(body.substr(0, MAX_PART_B64));
    text += _b64_text_parts(body);
}

int r_vss::mail_rings_port()
{
    auto port = r_std_utils::get_env("REVERE_MAIL_RINGS_PORT");
    if(!port.empty())
    {
        try
        {
            auto p = r_string_utils::s_to_int(port);
            if(p > 0 && p < 65536)
                return p;
        }
        catch(...) {}
    }
    return r_mail_rings_config().port;
}

r_mail_setup r_vss::mail_setup_for(const string& server, int port, const r_mail_login& login)
{
    r_mail_setup s;
    s.server = server;
    s.port = port;
    s.user = login.user;
    s.password = login.password;
    s.recipient = "rings@revere.local";
    return s;
}

bool r_vss::is_ring_mail(const string& subject, const string& text)
{
    return _lower(subject).find("someone rang the doorbell") != string::npos ||
           _lower(text).find("alarm event: visitor") != string::npos;
}

r_nullable<int64_t> r_vss::ring_subject_time_ms(const string& subject)
{
    // "YYYY/M/D H:MM:SS", the first in the subject, read by hand.
    const string s = subject.substr(0, MAX_SUBJECT);
    auto digits = [&](size_t& i, size_t lo, size_t hi, int& v) {
        size_t n = 0;
        v = 0;
        while (i < s.size() && n < hi && isdigit((unsigned char)s[i]))
        {
            v = v * 10 + (s[i] - '0');
            ++i;
            ++n;
        }
        return n >= lo && !(i < s.size() && isdigit((unsigned char)s[i]));
    };
    auto sep = [&](size_t& i, char c) { return i < s.size() && s[i] == c ? (++i, true) : false; };
    int f[6] = {0, 0, 0, 0, 0, 0};
    bool found = false;
    for (size_t start = 0; start < s.size() && !found; ++start)
    {
        if (!isdigit((unsigned char)s[start]) || (start > 0 && isdigit((unsigned char)s[start - 1])))
            continue;
        size_t i = start;
        found = digits(i, 4, 4, f[0]) && sep(i, '/') && digits(i, 1, 2, f[1]) && sep(i, '/') && digits(i, 1, 2, f[2]) &&
                sep(i, ' ') && digits(i, 1, 2, f[3]) && sep(i, ':') && digits(i, 2, 2, f[4]) && sep(i, ':') && digits(i, 2, 2, f[5]);
    }
    if (!found)
        return {};
    tm t {};
    t.tm_year = f[0] - 1900;
    t.tm_mon = f[1] - 1;
    t.tm_mday = f[2];
    t.tm_hour = f[3];
    t.tm_min = f[4];
    t.tm_sec = f[5];
    t.tm_isdst = -1;   // the box's time zone, its daylight saving as it was then
    auto secs = mktime(&t);
    if (secs == (time_t)-1)
        return {};
    return (int64_t)secs * 1000;
}

int64_t r_vss::ring_press_ms(const r_nullable<int64_t>& subject_ms, int64_t clock_offset_ms, int64_t arrival_ms, bool& clock_read)
{
    if (!subject_ms.is_null())
    {
        // The subject is the camera's clock: less its offset, it's ours.
        auto ours = subject_ms.value() - clock_offset_ms;
        if (ours >= arrival_ms - READ_BEFORE_MS && ours <= arrival_ms + READ_AFTER_MS)
        {
            clock_read = true;
            return ours;
        }
    }
    clock_read = false;
    return arrival_ms - USUAL_DELAY_MS;
}

r_mail_rings::r_mail_rings(r_mail_rings_config config, camera_for_ip_fn camera_for_ip, ring_fn on_ring,
                           clock_offset_fn clock_offset, now_fn now) :
    _config(std::move(config)),
    _camera_for_ip(std::move(camera_for_ip)),
    _on_ring(std::move(on_ring)),
    _clock_offset(std::move(clock_offset)),
    _now(now ? std::move(now) : now_fn(_wall_ms))
{
}

r_mail_rings::~r_mail_rings() noexcept
{
    stop();
}

void r_mail_rings::start()
{
    if (_server)
        return;
    if (_config.bind_ip.empty() || _config.bind_ip == "0.0.0.0" || _config.bind_ip == "::")
        R_THROW(("The mail ring listener binds to the box's own LAN address, never to every address."));
    if (_config.user.empty() || _config.password.empty() || _config.cert_pem.empty() || _config.key_pem.empty())
        R_THROW(("The mail ring listener needs its user, password and certificate."));
    _tls = make_shared<r_ssl_server_config>(_config.cert_pem, _config.key_pem);
    // Every worker busy and the queue full: refused with 421 (each of the max_connections may be slow).
    _server = make_unique<r_server_threaded<r_ssl_server_socket>>(
        _config.port, [this](r_ssl_server_socket& conn) { _serve(conn); }, _config.bind_ip, _config.max_connections, 1);
    _server->get_socket().set_config(_tls);
    _server->get_socket().set_io_timeout((uint64_t)_config.command_timeout_ms);
    _server->set_overload_cb([](r_ssl_server_socket& conn) {
        try { conn.start_plain(); const char* busy = "421 4.3.2 Too many connections\r\n"; conn.send(busy, strlen(busy)); } catch (...) {}
    });
    _accept_thread = thread([this]() { _server->start(); });
}

void r_mail_rings::stop()
{
    if (!_server)
        return;
    _server->stop();
    if (_accept_thread.joinable())
        _accept_thread.join();
    _server->join_all();
    _server.reset();
}

r_mail_rings::counts r_mail_rings::status() const
{
    lock_guard<mutex> g(_lock);
    return _counts;
}

void r_mail_rings::_serve(r_ssl_server_socket& conn)
{
    try
    {
        conn.start_plain();
    }
    catch (...)
    {
        return;
    }
    const string ip = conn.get_peer_ip();
    auto camera = _camera_for_ip ? _camera_for_ip(ip) : r_nullable<string>();
    auto say = [&](const string& line) {
        string out = line + "\r\n";
        return conn.send(out.data(), out.size()) == (int)out.size();
    };
    if (camera.is_null())
    {
        bool log = false;
        {
            lock_guard<mutex> g(_lock);
            ++_counts.refused;
            auto now = _now();
            // Kept only as long as they matter: an older one would be logged again anyway.
            for (auto it = _stranger_logged.begin(); it != _stranger_logged.end();)
                it = now - it->second >= STRANGER_LOG_EVERY_MS ? _stranger_logged.erase(it) : next(it);
            auto found = _stranger_logged.find(ip);
            if (found == _stranger_logged.end())
            {
                _stranger_logged[ip] = now;
                log = true;
            }
        }
        if (log)
            R_LOG_WARNING("mail rings: refused a connection from %s: no doorbell there sends rings by email", ip.c_str());
        say("554 5.7.1 Not accepted");
        conn.close();
        return;
    }

    string buf;
    // The whole connection has connection_timeout_ms, however steadily the client sends: one that trickles a line at a
    // time, or a DATA without its end, can't hold a slot for ever.
    const auto deadline = steady_clock::now() + milliseconds(_config.connection_timeout_ms);
    // One line, without its CRLF; false when the connection ended, a line ran too long, or the client was too slow.
    auto line_of = [&](string& line) {
        for (;;)
        {
            auto nl = buf.find('\n');
            if (nl != string::npos)
            {
                line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                return true;
            }
            if (buf.size() > MAX_LINE * 4)
                return false;
            auto left = duration_cast<milliseconds>(deadline - steady_clock::now()).count();
            if (left <= 0)
                return false;
            uint64_t wait = (uint64_t)min<int64_t>(_config.command_timeout_ms, left);
            if (!conn.wait_till_recv_wont_block(wait))
                return false;
            char chunk[4096];
            int n = conn.recv(chunk, sizeof(chunk));
            if (n <= 0)
                return false;
            buf.append(chunk, (size_t)n);
        }
    };

    bool tls = false, authed = false, from = false, to = false;
    int bad_logins = 0;
    if (!say("220 revere ESMTP"))
        return;
    string line;
    while (line_of(line))
    {
        if (line.size() > MAX_LINE)
        {
            say("500 5.5.6 Line too long");
            break;
        }
        auto sp = line.find(' ');
        string verb = _lower(line.substr(0, sp));
        string arg = sp == string::npos ? string() : line.substr(sp + 1);
        if (verb == "ehlo")
        {
            from = to = false;
            say("250-revere");
            say(tls ? "250-AUTH PLAIN LOGIN" : "250-STARTTLS");
            say("250 SIZE " + to_string(_config.max_mail_bytes));
        }
        else if (verb == "helo")
        {
            from = to = false;
            say("250 revere");
        }
        else if (verb == "starttls")
        {
            if (tls)
            {
                say("503 5.5.1 TLS already started");
                continue;
            }
            // Nothing may follow STARTTLS before the handshake (RFC 3207): what did was sent in the clear.
            if (!buf.empty())
                break;
            say("220 2.0.0 Ready to start TLS");
            try
            {
                conn.start_tls();
            }
            catch (const exception& e)
            {
                lock_guard<mutex> g(_lock);
                _counts.last_error = string("TLS: ") + e.what();
                break;
            }
            tls = true;
            authed = from = to = false;
        }
        else if (verb == "auth")
        {
            if (!tls)
            {
                say("530 5.7.0 Must issue a STARTTLS command first");
                continue;
            }
            if (authed)
            {
                say("503 5.5.1 Already authenticated");
                continue;
            }
            auto sp2 = arg.find(' ');
            string mech = _lower(arg.substr(0, sp2));
            string initial = sp2 == string::npos ? string() : arg.substr(sp2 + 1);
            string user, pass;
            bool ok = true;
            if (mech == "plain")
            {
                if (initial.empty())
                {
                    say("334 ");
                    ok = line_of(initial);
                }
                auto creds = _b64_text(initial);
                auto a = creds.find('\0');
                auto b = a == string::npos ? string::npos : creds.find('\0', a + 1);
                if (b != string::npos)
                {
                    user = creds.substr(a + 1, b - a - 1);
                    pass = creds.substr(b + 1);
                }
            }
            else if (mech == "login")
            {
                string u, p;
                if (initial.empty())
                {
                    say("334 VXNlcm5hbWU6");
                    ok = line_of(u);
                }
                else
                    u = initial;
                if (ok)
                {
                    say("334 UGFzc3dvcmQ6");
                    ok = line_of(p);
                }
                user = _b64_text(u);
                pass = _b64_text(p);
            }
            else
            {
                say("504 5.5.4 Unrecognized authentication type");
                continue;
            }
            if (!ok)
                break;
            // Too many wrong logins from this address lately (over connections): refused unchecked, for a while.
            bool barred = false;
            {
                lock_guard<mutex> g(_lock);
                auto now = _now();
                auto& tries = _bad_login_times[ip];
                while (!tries.empty() && now - tries.front() >= BAD_LOGIN_WINDOW_MS)
                    tries.pop_front();
                barred = (int)tries.size() >= _config.bad_logins_per_address;
                if (tries.empty())
                    _bad_login_times.erase(ip);
            }
            if (barred)
            {
                say("454 4.7.0 Too many failed logins, try again later");
                break;
            }
            // Both compared, whichever is wrong.
            bool user_ok = _same(user, _config.user), pass_ok = _same(pass, _config.password);
            if (user_ok && pass_ok)
            {
                authed = true;
                say("235 2.7.0 Authentication successful");
            }
            else
            {
                {
                    lock_guard<mutex> g(_lock);
                    ++_counts.bad_logins;
                    _bad_login_times[ip].push_back(_now());
                }
                say("535 5.7.8 Authentication credentials invalid");
                if (++bad_logins >= MAX_BAD_LOGINS)
                    break;
            }
        }
        else if (verb == "mail")
        {
            if (!tls || !authed)
            {
                say(tls ? "530 5.7.0 Authentication required" : "530 5.7.0 Must issue a STARTTLS command first");
                continue;
            }
            from = true;
            to = false;
            say("250 2.1.0 OK");
        }
        else if (verb == "rcpt")
        {
            if (!from)
            {
                say("503 5.5.1 Need MAIL first");
                continue;
            }
            to = true;
            say("250 2.1.5 OK");
        }
        else if (verb == "data")
        {
            if (!to)
            {
                say("503 5.5.1 Need RCPT first");
                continue;
            }
            say("354 End data with <CR><LF>.<CR><LF>");
            string message;
            bool too_big = false, ended = false;
            size_t seen = 0;
            string l;
            while (line_of(l))
            {
                if (l == ".")
                {
                    ended = true;
                    break;
                }
                if (!l.empty() && l[0] == '.')
                    l.erase(0, 1);
                seen += l.size() + 2;
                // Too big: read to its end, keeping none of it; but not without end.
                if (seen > 2 * _config.max_mail_bytes)
                    break;
                if (message.size() + l.size() + 2 > _config.max_mail_bytes)
                    too_big = true;
                else if (!too_big)
                    message += l + "\r\n";
            }
            if (!ended)
            {
                if (seen > 2 * _config.max_mail_bytes)
                    say("552 5.3.4 Message too big");
                break;
            }
            from = to = false;
            if (too_big)
            {
                say("552 5.3.4 Message too big");
                continue;
            }
            _mail(camera.value(), message);
            say("250 2.0.0 OK");
        }
        else if (verb == "rset")
        {
            from = to = false;
            say("250 2.0.0 OK");
        }
        else if (verb == "noop")
            say("250 2.0.0 OK");
        else if (verb == "quit")
        {
            say("221 2.0.0 Bye");
            break;
        }
        else
            say("502 5.5.2 Command not recognized");
    }
    conn.close();
}

void r_mail_rings::_mail(const string& camera_id, const string& message)
{
    auto arrival = _now();
    {
        lock_guard<mutex> g(_lock);
        auto& recent = _recent[camera_id];
        while (!recent.empty() && arrival - recent.front() >= 60 * 1000)
            recent.pop_front();
        if ((int)recent.size() >= _config.mails_per_minute)
        {
            ++_counts.rate_limited;
            return;
        }
        recent.push_back(arrival);
    }
    string subject, text;
    read_mail(message, subject, text);
    if (!is_ring_mail(subject, text))
    {
        lock_guard<mutex> g(_lock);
        ++_counts.other_mail;
        return;
    }
    r_mail_ring ring;
    ring.camera_id = camera_id;
    ring.arrival_ms = arrival;
    ring.press_ms = ring_press_ms(ring_subject_time_ms(subject), _clock_offset ? _clock_offset(camera_id) : 0, arrival, ring.clock_read);
    {
        lock_guard<mutex> g(_lock);
        ++_counts.rings;
        _counts.last_ring_ms = ring.press_ms;
    }
    R_LOG_INFO("mail rings: ring by email from %s (%d s to arrive%s)", camera_id.c_str(), (int)((arrival - ring.press_ms) / 1000),
               ring.clock_read ? "" : ", its time not read");
    if (_on_ring)
        _on_ring(ring);
}

map<string, string> r_vss::mail_ring_doorbells(const vector<r_disco::r_camera>& cameras)
{
    map<string, string> out;
    for (auto& c : cameras)
        if (r_disco::camera_kind(c) == "doorbell" && r_disco::camera_press_source(c) == "reolink_email" &&
            !c.ipv4.is_null() && !c.ipv4.value().empty())
            out[c.ipv4.value()] = c.id;
    return out;
}

static string _random_text(size_t chars)
{
    // Letters and digits that can't be misread when typed from a screen into an app.
    static const char ALPHABET[] = "abcdefghjkmnpqrstuvwxyz23456789";
    vector<unsigned char> bytes(chars);
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    const char* pers = "r_mail_rings";
    int ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)pers, strlen(pers));
    if (ret == 0)
        ret = mbedtls_ctr_drbg_random(&drbg, bytes.data(), bytes.size());
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    if (ret != 0)
        R_THROW(("Unable to make a random login for the mail listener (%d).", ret));
    string out;
    for (auto b : bytes)
        out += ALPHABET[b % (sizeof(ALPHABET) - 1)];
    return out;
}

r_mail_login r_vss::load_or_make_mail_login(const string& path)
{
    if (r_fs::file_exists(path))
    {
        auto bytes = r_fs::read_file(path);
        string text(bytes.begin(), bytes.end());
        auto nl = text.find('\n');
        if (nl != string::npos)
        {
            r_mail_login login{text.substr(0, nl), text.substr(nl + 1)};
            while (!login.password.empty() && (login.password.back() == '\n' || login.password.back() == '\r'))
                login.password.pop_back();
            if (!login.user.empty() && !login.password.empty())
                return login;
        }
        R_LOG_WARNING("mail rings: the stored login in %s is unreadable; making a new one", path.c_str());
    }
    r_mail_login login{"revere-" + _random_text(6), _random_text(20)};
    string dir, name;
    r_fs::break_path(path, dir, name);
    r_fs::mkdir_p(dir);
    string text = login.user + "\n" + login.password + "\n";
    r_fs::atomic_write_private_file((const uint8_t*)text.data(), text.size(), path);
    return login;
}

pair<string, string> r_vss::load_or_make_mail_cert(const string& cert_path, const string& key_path)
{
    if (r_fs::file_exists(cert_path) && r_fs::file_exists(key_path))
    {
        try
        {
            auto c = r_fs::read_file(cert_path);
            auto k = r_fs::read_file(key_path);
            pair<string, string> pems{string(c.begin(), c.end()), string(k.begin(), k.end())};
            r_ssl_server_config check(pems.first, pems.second);   // throws if they're no good
            return pems;
        }
        catch (const exception& e)
        {
            R_LOG_WARNING("mail rings: the stored certificate is unusable (%s); making a new one", e.what());
        }
    }
    auto pems = r_ssl_server_config::generate_self_signed("revere");
    string dir, name;
    r_fs::break_path(cert_path, dir, name);
    r_fs::mkdir_p(dir);
    r_fs::atomic_write_file((const uint8_t*)pems.first.data(), pems.first.size(), cert_path);
    r_fs::atomic_write_private_file((const uint8_t*)pems.second.data(), pems.second.size(), key_path);
    return pems;
}
