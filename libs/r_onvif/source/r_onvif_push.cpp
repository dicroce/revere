#ifdef IS_WINDOWS
#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#endif

#include "r_onvif/r_onvif_push.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_server_threaded.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <stdexcept>

#ifdef IS_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

using namespace r_onvif;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

const char* const r_onvif::PUSH_PATH_PREFIX = "/onvif/notify/";

push_head r_onvif::parse_push_head(const string& head)
{
    push_head h;

    // The request line: POST <path> HTTP/1.x
    auto eol = head.find("\r\n");
    if (eol == string::npos)
    {
        h.status = 400;
        return h;
    }
    const string line = head.substr(0, eol);
    auto sp1 = line.find(' ');
    auto sp2 = sp1 == string::npos ? string::npos : line.find(' ', sp1 + 1);
    if (sp1 == string::npos || sp2 == string::npos)
    {
        h.status = 400;
        return h;
    }
    const string method = line.substr(0, sp1);
    string path = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const string version = line.substr(sp2 + 1);
    if (version.compare(0, 5, "HTTP/") != 0)
    {
        h.status = 400;
        return h;
    }
    if (method != "POST")
    {
        h.status = 405;
        return h;
    }

    // The path is the prefix and the id, and nothing else (a query is ignored). Or "/", which is what some cameras post
    // to whatever address they were given (they keep only the host and port): it has no id, and the caller's address
    // says whose it is.
    auto q = path.find('?');
    if (q != string::npos)
        path = path.substr(0, q);
    const string prefix = PUSH_PATH_PREFIX;
    if (path == "/")
    {
        h.id.clear();
    }
    else
    {
        if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0)
        {
            h.status = 404;
            return h;
        }
        h.id = path.substr(prefix.size());
        if (h.id.size() > 64 || !all_of(h.id.begin(), h.id.end(), [](unsigned char c) { return isalnum(c) || c == '-' || c == '_'; }))
        {
            h.status = 404;
            h.id.clear();
            return h;
        }
    }

    // The headers: Content-Length is required (a chunked body isn't read), and is checked before anything is read for it.
    bool have_length = false;
    size_t pos = eol + 2;
    while (pos < head.size())
    {
        auto next = head.find("\r\n", pos);
        if (next == string::npos || next == pos)
            break;
        const string header = head.substr(pos, next - pos);
        pos = next + 2;
        auto colon = header.find(':');
        if (colon == string::npos)
            continue;
        const string name = r_string_utils::to_lower(header.substr(0, colon));
        string value = r_string_utils::strip(header.substr(colon + 1));
        if (name == "transfer-encoding")
        {
            h.status = 411;
            return h;
        }
        if (name == "content-length")
        {
            if (have_length || value.empty() || value.size() > 9 || !all_of(value.begin(), value.end(), [](unsigned char c) { return isdigit(c); }))
            {
                h.status = 400;
                return h;
            }
            h.content_length = (size_t)stoull(value);
            have_length = true;
        }
    }
    if (!have_length)
    {
        h.status = 411;
        return h;
    }
    if (h.content_length > PUSH_MAX_BODY)
    {
        h.status = 413;
        return h;
    }
    return h;
}

string r_onvif::new_push_id()
{
    random_device rd;
    static const char* hex = "0123456789abcdef";
    string id;
    for (int i = 0; i < 16; ++i)
    {
        unsigned v = rd() & 0xff;
        id += hex[v >> 4];
        id += hex[v & 15];
    }
    return id;
}

bool r_onvif::is_ipv4_literal(const string& s)
{
    in_addr a;
    return !s.empty() && s.size() <= 15 && inet_pton(AF_INET, s.c_str(), &a) == 1;
}

string r_onvif::local_ip_for(const string& camera_ip)
{
    if (!is_ipv4_literal(camera_ip))
        return string();
    // A bench or a test with its camera on this computer: say which address to listen on.
    if (const char* forced = getenv("REVERE_ONVIF_PUSH_BIND"))
        if (is_ipv4_literal(forced))
            return forced;
    r_raw_socket::socket_startup();

#ifdef IS_WINDOWS
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET)
        return string();
    auto close_it = [&]() { ::closesocket(s); };
#else
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return string();
    auto close_it = [&]() { ::close(s); };
#endif
    string out;
    sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(9);
    inet_pton(AF_INET, camera_ip.c_str(), &to.sin_addr);
    // A UDP connect sends nothing: it only has the OS pick the route, and so the address, for this destination.
    if (::connect(s, (sockaddr*)&to, sizeof(to)) == 0)
    {
        sockaddr_in me;
        memset(&me, 0, sizeof(me));
        socklen_t len = sizeof(me);
        if (::getsockname(s, (sockaddr*)&me, &len) == 0)
        {
            char buf[INET_ADDRSTRLEN] = {0};
            if (inet_ntop(AF_INET, &me.sin_addr, buf, sizeof(buf)))
                out = buf;
        }
    }
    close_it();
    if (out == "0.0.0.0" || out.compare(0, 4, "127.") == 0)
        return string();
    return out;
}

struct r_onvif_push_listener::impl
{
    struct entry
    {
        string camera_ip;
        push_handler handler;
    };

    impl(r_onvif_push_listener* owner, int port, const string& ip) :
        server(port, [owner](r_socket& conn) { owner->_serve(&conn); }, ip, 4, 16)
    {
    }

    r_server_threaded<r_socket> server;
    thread th;
    mutex lock;
    condition_variable idle;        // signalled when a handler call ends
    int calls {0};                  // handler calls running now
    map<string, entry> entries;
    atomic<size_t> rejected {0};
};

r_onvif_push_listener::r_onvif_push_listener(const string& local_ip, int first, int last) :
    _local_ip(local_ip)
{
    r_raw_socket::socket_startup();
    // The first port in the range that binds on this address.
    for (int port = first; port <= last && !_impl; ++port)
    {
        try
        {
            r_socket probe;
            probe.create(AF_INET);
            probe.bind(port, local_ip);
            probe.close();
        }
        catch (...)
        {
            continue;
        }
        _port = port;
        _impl.reset(new impl(this, port, local_ip));
    }
    if (!_impl)
        throw runtime_error("no port from " + to_string(first) + " to " + to_string(last) + " is free on " + local_ip);

    auto* i = _impl.get();
    i->th = thread([i]() { i->server.start(); });

    // start() returns before the server listens: wait until it takes a connection.
    for (int n = 0; n < 200; ++n)
    {
        try
        {
            r_socket probe;
            probe.connect(local_ip, _port);
            probe.close();
            return;
        }
        catch (...)
        {
            this_thread::sleep_for(milliseconds(10));
        }
    }
    i->server.stop();
    i->th.join();
    throw runtime_error("the listener on " + local_ip + ":" + to_string(_port) + " didn't start");
}

r_onvif_push_listener::~r_onvif_push_listener() noexcept
{
    if (!_impl)
        return;
    _impl->server.stop();
    if (_impl->th.joinable())
        _impl->th.join();
    _impl->server.join_all();
}

shared_ptr<r_onvif_push_listener> r_onvif_push_listener::acquire(const string& local_ip, int first, int last)
{
    // One per local address, while anyone holds it.
    static mutex lock;
    static map<string, weak_ptr<r_onvif_push_listener>> listeners;
    lock_guard<mutex> g(lock);
    auto found = listeners.find(local_ip);
    if (found != listeners.end())
        if (auto l = found->second.lock())
            return l;
    auto l = make_shared<r_onvif_push_listener>(local_ip, first, last);
    listeners[local_ip] = l;
    return l;
}

void r_onvif_push_listener::add(const string& id, const string& camera_ip, push_handler handler)
{
    lock_guard<mutex> g(_impl->lock);
    _impl->entries[id] = impl::entry{camera_ip, std::move(handler)};
}

void r_onvif_push_listener::remove(const string& id)
{
    unique_lock<mutex> g(_impl->lock);
    _impl->entries.erase(id);
    // A handler that is running may use what the caller is about to free: wait for it. (A handler is quick: it queues.)
    _impl->idle.wait(g, [this]() { return _impl->calls == 0; });
}

string r_onvif_push_listener::url(const string& id) const
{
    return "http://" + _local_ip + ":" + to_string(_port) + PUSH_PATH_PREFIX + id;
}

size_t r_onvif_push_listener::rejected() const
{
    return _impl->rejected.load();
}

static void _reply(r_socket& conn, int status)
{
    const char* text = status == 200 ? "OK" : status == 400 ? "Bad Request" : status == 403 ? "Forbidden" : status == 404 ? "Not Found" :
                       status == 405 ? "Method Not Allowed" : status == 411 ? "Length Required" : status == 413 ? "Payload Too Large" :
                       "Error";
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", status, text);
    try { conn.send(buf, (size_t)n); } catch (...) {}
}

void r_onvif_push_listener::_serve(void* c)
{
    auto& conn = *static_cast<r_socket*>(c);
    auto turn_away = [&](int status) {
        ++_impl->rejected;
        if (status)
            _reply(conn, status);
    };

    try
    {
        // Who is calling, before reading a byte: a caller that isn't one of the cameras subscribed gets nothing.
        const string peer = conn.get_peer_ip();
        {
            lock_guard<mutex> g(_impl->lock);
            bool known = false;
            for (auto& e : _impl->entries)
                if (e.second.camera_ip == peer)
                    known = true;
            if (!known)
            {
                turn_away(0);
                return;
            }
        }

        const auto deadline = steady_clock::now() + milliseconds(PUSH_READ_DEADLINE_MS);
        auto read_more = [&](string& into, size_t cap) -> bool {
            auto left = duration_cast<milliseconds>(deadline - steady_clock::now()).count();
            if (left <= 0)
                return false;
            uint64_t wait_ms = (uint64_t)left;
            if (!conn.wait_till_recv_wont_block(wait_ms))
                return false;
            char buf[4096];
            int n = conn.recv(buf, min(sizeof(buf), cap));
            if (n <= 0)
                return false;
            into.append(buf, (size_t)n);
            return true;
        };

        // The head: up to the blank line, at most PUSH_MAX_HEAD bytes.
        string in;
        size_t end = string::npos;
        while ((end = in.find("\r\n\r\n")) == string::npos)
        {
            if (in.size() >= PUSH_MAX_HEAD || !read_more(in, PUSH_MAX_HEAD - in.size()))
            {
                turn_away(in.size() >= PUSH_MAX_HEAD ? 413 : 400);
                return;
            }
        }
        const string head = in.substr(0, end + 4);
        string body = in.substr(end + 4);

        auto h = parse_push_head(head);
        if (h.status)
        {
            turn_away(h.status);
            return;
        }

        // An id belongs to one camera, and this is it. With no id (the path "/"), the caller's address picks the
        // subscription, and only when it has exactly one. Returns 0 and the handler, or the status to turn away with.
        // With count_call, the handler's call is counted (under the same lock) so remove() waits for it.
        auto resolve = [&](push_handler& handler, bool count_call) -> int {
            lock_guard<mutex> g(_impl->lock);
            if (h.id.empty())
            {
                size_t n = 0;
                for (auto& e : _impl->entries)
                    if (e.second.camera_ip == peer)
                    {
                        handler = e.second.handler;
                        ++n;
                    }
                if (n != 1)
                    return 404;
            }
            else
            {
                auto found = _impl->entries.find(h.id);
                if (found == _impl->entries.end())
                    return 404;
                if (found->second.camera_ip != peer)
                    return 403;
                handler = found->second.handler;
            }
            if (count_call)
                ++_impl->calls;
            return 0;
        };
        {
            push_handler early;
            if (int status = resolve(early, false))
            {
                turn_away(status);
                return;
            }
        }

        // The body: exactly Content-Length bytes (what comes after is not read).
        if (body.size() > h.content_length)
            body.resize(h.content_length);
        while (body.size() < h.content_length)
        {
            if (!read_more(body, h.content_length - body.size()))
            {
                turn_away(400);
                return;
            }
        }

        // Looked up again: while the body was coming, the subscription may have ended (and its owner gone).
        push_handler handler;
        if (int status = resolve(handler, true))
        {
            turn_away(status);
            return;
        }
        struct done_call
        {
            r_onvif_push_listener::impl& i;
            ~done_call()
            {
                { lock_guard<mutex> g(i.lock); --i.calls; }
                i.idle.notify_all();
            }
        } done{*_impl};
        handler(body);
        _reply(conn, 200);
    }
    catch (const exception& e)
    {
        ++_impl->rejected;
        R_LOG_WARNING("ONVIF push listener: %s", e.what());
    }
    catch (...)
    {
        ++_impl->rejected;
    }
}
