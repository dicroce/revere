#include "r_vss/r_mail_rings.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_server_threaded.h"
#include "r_utils/r_ssl_server_socket.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <chrono>
#include <ctime>
#include <regex>

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

namespace
{

const size_t MAX_LINE = 1000;               // RFC 5321's line limit, with room
const int MAX_BAD_LOGINS = 3;               // per connection
const int64_t STRANGER_LOG_EVERY_MS = 10 * 60 * 1000;
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

// RFC 2047 encoded words in a header: =?charset?B?...?= and =?charset?Q?...?=, the text taken as UTF-8.
string _decode_words(const string& v)
{
    static const regex word(R"(=\?[^?]+\?([bBqQ])\?([^?]*)\?=)");
    string out;
    auto begin = sregex_iterator(v.begin(), v.end(), word);
    size_t at = 0;
    for (auto it = begin; it != sregex_iterator(); ++it)
    {
        auto& m = *it;
        string between = v.substr(at, (size_t)m.position() - at);
        // Space between two encoded words is not part of the text.
        if (!(at > 0 && between.find_first_not_of(" \t") == string::npos))
            out += between;
        string enc = m[1].str(), data = m[2].str();
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
        at = (size_t)(m.position() + m.length());
    }
    return out + v.substr(at);
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
        subject = _decode_words(headers["subject"]);
    // The body as sent, and decoded when it's base64 (top level, or a part's); a ring's words are found in either.
    text = body;
    if (_lower(headers["content-transfer-encoding"]).find("base64") != string::npos)
        text += "\n" + _b64_text(body);
    static const regex part_b64(R"(Content-Transfer-Encoding:\s*base64\s*\r?\n\r?\n([A-Za-z0-9+/=\r\n]+))", regex::icase);
    for (auto it = sregex_iterator(body.begin(), body.end(), part_b64); it != sregex_iterator(); ++it)
        text += "\n" + _b64_text((*it)[1].str());
}

bool r_vss::is_ring_mail(const string& subject, const string& text)
{
    return _lower(subject).find("someone rang the doorbell") != string::npos ||
           _lower(text).find("alarm event: visitor") != string::npos;
}

r_nullable<int64_t> r_vss::ring_subject_time_ms(const string& subject)
{
    static const regex at(R"((\d{4})/(\d{1,2})/(\d{1,2}) (\d{1,2}):(\d{2}):(\d{2}))");
    smatch m;
    if (!regex_search(subject, m, at))
        return {};
    tm t {};
    t.tm_year = stoi(m[1].str()) - 1900;
    t.tm_mon = stoi(m[2].str()) - 1;
    t.tm_mday = stoi(m[3].str());
    t.tm_hour = stoi(m[4].str());
    t.tm_min = stoi(m[5].str());
    t.tm_sec = stoi(m[6].str());
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
            auto& last = _stranger_logged[ip];
            if (now - last >= STRANGER_LOG_EVERY_MS)
            {
                last = now;
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
            uint64_t wait = (uint64_t)_config.command_timeout_ms;
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
                if (message.size() + l.size() + 2 > _config.max_mail_bytes)
                    too_big = true;   // read to its end, keep none of it
                else if (!too_big)
                    message += l + "\r\n";
            }
            if (!ended)
                break;
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
