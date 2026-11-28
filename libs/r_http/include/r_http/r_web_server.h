
#ifndef r_http_r_web_server_h
#define r_http_r_web_server_h

#include "r_http/r_server_request.h"
#include "r_http/r_server_response.h"
#include "r_http/r_methods.h"
#include "r_http/r_status_codes.h"
#include "r_http/r_http_exception.h"
#include "r_utils/r_server_threaded.h"
#include <chrono>
#include "r_utils/r_socket.h"
#include "r_utils/r_macro.h"
#include "r_utils/r_blob_tree.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_logger.h"
#include <functional>
#include <mutex>
#include <thread>
#include <map>
#include <string>
#include <vector>

#define WS_CATCH(type, code) \
    catch(type& ex) \
    { \
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__); \
        if(conn.valid()) \
        { \
            r_server_response response; \
            response.set_status_code(code); \
            response.write_response(conn); \
        } \
    }

// Client-error variant: logs the exception message only (first line of
// what()), without the captured backtrace, and at most a few such lines a
// second (r_http::log_client_error): 4xx responses are routine (unknown route,
// missing recording, bad token), and a client hammering bad requests would
// otherwise fill the log. The response says why as {"error": message}.
#define WS_CATCH_QUIET(type, code) \
    catch(type& ex) \
    { \
        auto why = r_http::first_line(ex.what()); \
        r_http::log_client_error(__FILE__, __LINE__, (int)code, why); \
        if(conn.valid()) \
            r_http::client_error_response(code, why).write_response(conn); \
    }

namespace r_http
{

// The first line of an exception's what(): r_exception appends its backtrace after it.
inline std::string first_line(const char* what)
{
    std::string s(what ? what : "");
    auto nl = s.find('\n');
    return (nl == std::string::npos) ? s : s.substr(0, nl);
}

// A client error's answer: the status, and {"error": why}.
inline r_server_response client_error_response(status_code code, const std::string& why)
{
    r_server_response response(code);
    response.set_content_type("application/json");
    std::string body = "{\"error\":\"";
    for(char c : why)
    {
        if(c == '"' || c == '\\') { body += '\\'; body += c; }
        else if((unsigned char)c < 0x20) body += ' ';
        else body += c;
    }
    body += "\"}";
    response.set_body(body);
    return response;
}

// How many client-error lines may be logged in each CLIENT_ERROR_LOG_WINDOW_MS; the rest are counted and said in one
// line when the next window opens.
static constexpr int CLIENT_ERROR_LOG_BURST = 20;
static constexpr int64_t CLIENT_ERROR_LOG_WINDOW_MS = 10000;

// One warning line per client error (no backtrace), rate-limited across the process.
inline void log_client_error(const char* file, int line, int code, const std::string& why)
{
    static std::mutex m;
    static int64_t window_start = 0;
    static int logged = 0;
    static int64_t left_out = 0;
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t report_left_out = 0;
    bool log = false;
    {
        std::lock_guard<std::mutex> g(m);
        if(now - window_start >= CLIENT_ERROR_LOG_WINDOW_MS)
        {
            report_left_out = left_out;
            left_out = 0;
            logged = 0;
            window_start = now;
        }
        if(logged < CLIENT_ERROR_LOG_BURST)
        {
            ++logged;
            log = true;
        }
        else
            ++left_out;
    }
    if(report_left_out > 0)
        R_LOG_WARNING("%lld more client errors were answered and not logged", (long long)report_left_out);
    if(log)
        R_LOG_WARNING("%s:%d %d %s", file, line, code, why.c_str());
}

template<class SOK_T>
class r_web_server;

template<class SOK_T>
class r_web_server final
{
public:
    typedef std::function<r_server_response(const r_web_server<SOK_T>& ws, SOK_T& conn, const r_server_request& request)> http_cb;

    r_web_server(
        int port,
        const std::string& sockAddr = std::string(),
        size_t workers = r_utils::r_server_threaded<SOK_T>::DEFAULT_WORKERS,
        size_t maxPending = r_utils::r_server_threaded<SOK_T>::DEFAULT_MAX_PENDING
    ) :
        _cbs(),
        _server(port, std::bind(&r_web_server<SOK_T>::_server_conn_cb, this, std::placeholders::_1), sockAddr, workers, maxPending),
        _serverThread()
    {
        // Every worker busy and the queue full: say so, so the client can
        // back off and retry, rather than leave it waiting.
        _server.set_overload_cb([](SOK_T& conn){
            // This runs on the accept thread, so nothing on this connection may
            // wait longer than the 50 ms budget below. Over HTTPS the first wait,
            // or the 503's send, runs the TLS handshake under the connection's
            // I/O timeout (10 s by default): a client that never sends a
            // ClientHello, or sends it slowly, stalled every accept that long.
            // A client that can't finish the handshake in time is just closed.
            conn.set_io_timeout(50);

            // Read what the client has sent first (briefly, without waiting
            // for a whole request): closing a socket with unread data makes
            // some stacks (Linux) reset the connection, and the client would
            // see a reset instead of the 503 and its Retry-After.
            // This runs on the accept thread, so it's bounded by a deadline
            // (not a budget decremented per wait, which a client trickling a
            // byte every fraction of a millisecond would never use up).
            char discard[4096];
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
            size_t drained = 0;
            while(drained < 65536)
            {
                auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
                if(left <= 0)
                    break;
                uint64_t wait_ms = (uint64_t)left;
                if(!conn.wait_till_recv_wont_block(wait_ms))
                    break;
                int n = conn.recv(discard, sizeof(discard));
                if(n <= 0)
                    break;
                drained += (size_t)n;
            }

            r_server_response response(response_service_unavailable);
            response.add_additional_header("Retry-After", "1");
            response.write_response(conn, 250);
        });
    }

    r_web_server(const r_web_server<SOK_T>&) = delete;

    ~r_web_server() noexcept
    {
        stop();
    }

    r_web_server<SOK_T>& operator = (const r_web_server<SOK_T>&) = delete;

    void start()
    {
        _serverThread = std::thread(&r_utils::r_server_threaded<SOK_T>::start, &_server);
    }

    // Safe whether or not start() was called, whether or not the server came
    // up (its port may have been taken), and more than once. Never throws: the
    // destructor calls it.
    void stop() noexcept
    {
        _server.stop();
        // Join whatever start() launched, even if the server never got going
        // (a bind failure returns from the accept loop at once): destroying a
        // joinable std::thread calls std::terminate.
        if( _serverThread.joinable() )
            _serverThread.join();
        // The accept loop has now exited: drain the in-flight request handlers
        // before returning, so a caller can then destroy state they use (e.g.
        // r_ws's transcode _sessions).
        _server.join_all();
    }

    // True once the server has begun shutting down. A route handler that loops
    // for a while (e.g. a transcode decode over a clip) can poll this each
    // iteration and return early, so it doesn't make shutdown wait for it.
    bool stopping() const { return _server.stopping(); }

    // The largest request body this server reads (r_server_request::DEFAULT_MAX_BODY_SIZE unless set). A request with
    // a larger one is answered 413 and its connection closed, before its body is read and before any route runs. Set
    // it before start().
    void set_max_request_body( size_t max_body_size )
    {
        _maxBodySize = max_body_size;
    }

    size_t max_request_body() const { return _maxBodySize; }

    // How long a request may take to arrive in all, headers and body (DEFAULT_REQUEST_READ_DEADLINE_MS unless set; 0
    // is no limit). A request still arriving past it is answered 408 and its connection closed, so a worker isn't
    // held by one sent slowly. Set it before start().
    static constexpr uint64_t DEFAULT_REQUEST_READ_DEADLINE_MS = 30000;
    void set_request_read_deadline( uint64_t deadline_millis )
    {
        _readDeadlineMs = deadline_millis;
    }

    uint64_t request_read_deadline() const { return _readDeadlineMs; }

    void add_route( int method, const std::string& path, http_cb cb )
    {
        _cbs[method][path] = cb;
    }

    // Serve a pre-packed r_blob_tree bundle under url_prefix.
    // Bundle format (per file): node["bytes"]=content, node["mime"]=mime-type, node["etag"]=hash.
    // Supports ETag-based 304 responses and CORS preflight (OPTIONS).
    // Unknown paths that look like SPA routes (no file extension) fall back to index.html.
    void serve_bundle(const std::string& url_prefix, r_utils::r_blob_tree bundle)
    {
        _bundles[url_prefix] = std::move(bundle);
        const r_utils::r_blob_tree* bundlePtr = &_bundles[url_prefix];

        add_route(METHOD_GET, url_prefix, [bundlePtr, url_prefix](const r_web_server<SOK_T>&, SOK_T&, const r_server_request& request) -> r_server_response {
            auto path = request.get_uri().get_full_resource_path();

            std::string relative = path.substr(url_prefix == "/" ? 1 : url_prefix.size());
            while(!relative.empty() && relative.front() == '/')
                relative = relative.substr(1);
            if(relative.empty())
                relative = "index.html";

            std::vector<std::string> segments;
            std::string seg;
            for(char c : relative)
            {
                if(c == '/') { if(!seg.empty()) { segments.push_back(seg); seg.clear(); } }
                else seg += c;
            }
            if(!seg.empty()) segments.push_back(seg);

            const r_utils::r_blob_tree* node = bundlePtr;
            for(const auto& s : segments)
            {
                if(!node->has_key(s))
                {
                    // SPA fallback: no extension means a client-side route — serve index.html
                    if(s.find('.') == std::string::npos && bundlePtr->has_key("index.html"))
                        node = &bundlePtr->at("index.html");
                    else
                        R_STHROW(r_http_404_exception, ("File not found in bundle: %s", path.c_str()));
                    break;
                }
                node = &node->at(s);
            }

            const auto& etag = node->at("etag").get_string();
            auto if_none_match = request.get_header("if-none-match");
            if(!if_none_match.is_null() && if_none_match.value() == etag)
                return r_server_response(response_not_modified);

            const auto& bytes = node->at("bytes").get_blob();
            const auto& mime  = node->at("mime").get_string();

            r_server_response response;
            response.set_content_type(mime);
            response.set_body(bytes.size(), bytes.data());
            response.add_additional_header("ETag", etag);
            response.add_additional_header("Cache-Control", "no-cache");
            return response;
        });

        add_route(METHOD_OPTIONS, url_prefix, [](const r_web_server<SOK_T>&, SOK_T&, const r_server_request&) -> r_server_response {
            r_server_response response(response_no_content);
            response.add_additional_header("Access-Control-Allow-Origin", "*");
            response.add_additional_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            response.add_additional_header("Access-Control-Allow-Headers", "Content-Type, Authorization, If-None-Match");
            return response;
        });
    }

    SOK_T& get_socket() { return _server.get_socket(); }

private:
    void _server_conn_cb(SOK_T& conn)
    {
        r_server_request request;
        request.set_max_body_size(_maxBodySize);
        request.set_read_deadline(_readDeadlineMs);
        try
        {
            request.read_request(conn);
        }
        catch(r_http_413_exception& ex)
        {
            R_LOG_WARNING("%s:%d %s", __FILE__, __LINE__, r_utils::r_string_utils::split(std::string(ex.what()), '\n').front().c_str());
            _refuse(conn, response_payload_too_large);
            return;
        }
        catch(r_http_408_exception& ex)
        {
            R_LOG_WARNING("%s:%d %s", __FILE__, __LINE__, r_utils::r_string_utils::split(std::string(ex.what()), '\n').front().c_str());
            _refuse(conn, response_request_timeout);
            return;
        }
        catch(r_http_400_exception& ex)
        {
            // A length that isn't a number.
            R_LOG_WARNING("%s:%d %s", __FILE__, __LINE__, r_utils::r_string_utils::split(std::string(ex.what()), '\n').front().c_str());
            if(conn.valid())
            {
                r_server_response response(response_bad_request);
                try { response.write_response(conn, 1000); } catch(...) {}
            }
            return;
        }

        // The request may have waited in the queue for a free worker. If the
        // client has given up in the meantime (its own timeout), nobody will
        // read the response: don't do the work. (A client that half-closes its
        // side after sending the request, like `nc -N`, looks gone too; none
        // of revere's clients do that.)
        if( conn.peer_closed() )
            return;

        r_uri ruri = request.get_uri();

        try
        {
            auto foundMethod = _cbs.find(request.get_method());

            if( foundMethod == _cbs.end() )
                R_STHROW( r_http_404_exception, ("No routes match request method type.") );

            auto path = ruri.get_full_resource_path();

            auto foundRoute = foundMethod->second.end();

            for(auto i = foundMethod->second.begin(), e = foundMethod->second.end(); i != e; ++i)
            {
                if(path.compare(0, i->first.length(), i->first) == 0)
                    foundRoute = i;
            }

            if( foundRoute == foundMethod->second.end() )
                R_STHROW( r_http_404_exception, ("Unable to found path: %s", path.c_str()) );

            auto response = foundRoute->second(*this, conn, request);

            if(!response.written() && conn.valid())
                response.write_response(conn);
        }
        WS_CATCH_QUIET(r_http_400_exception, response_bad_request)
        WS_CATCH_QUIET(r_http_401_exception, response_unauthorized)
        WS_CATCH_QUIET(r_http_403_exception, response_forbidden)
        WS_CATCH_QUIET(r_http_404_exception, response_not_found)
        WS_CATCH(r_http_500_exception, response_internal_server_error)
        WS_CATCH(r_http_501_exception, response_not_implemented)
        WS_CATCH_QUIET(r_utils::r_not_found_exception, response_not_found)
        WS_CATCH_QUIET(r_utils::r_invalid_argument_exception, response_bad_request)
        WS_CATCH_QUIET(r_utils::r_unauthorized_exception, response_forbidden)
        WS_CATCH(r_utils::r_not_implemented_exception, response_not_implemented)
        WS_CATCH(r_utils::r_timeout_exception, response_internal_server_error)
        WS_CATCH(r_utils::r_io_exception, response_internal_server_error)
        WS_CATCH(r_utils::r_internal_exception, response_internal_server_error)
        catch(r_http_exception& ex)
        {
            // Any other status a handler chose (507 for a full disk, say): answered with it and {"error": why}.
            // A stack trace only for a server fault; a refusal (507, 503) is one line, like a client error.
            auto code = (status_code)ex.get_status();
            auto why = first_line(ex.what());
            if(code >= 500 && code != response_insufficient_storage && code != response_service_unavailable)
                R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
            else
                log_client_error(__FILE__, __LINE__, (int)code, why);
            if(conn.valid())
                client_error_response(code, why).write_response(conn);
        }
        catch(std::exception& ex)
        {
            // Note: we cannot write a response here because this exception
            // might indicate our sockets are not invalid simply because of
            // and incomplete io... but nevertheless our sockets might not
            // be able to communicate becuase the other side make have
            // broken its connection.
            R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
        }
        catch(...)
        {
            // Note: we cannot write a response here because this exception
            // might indicate our sockets are not invalid simply because of
            // and incomplete io... but nevertheless our sockets might not
            // be able to communicate becuase the other side make have
            // broken its connection.

            R_LOG_NOTICE("An unknown exception has occurred in our web server.");
        }
    }

    // A request refused before it was read whole (413, 408): answered, and the connection closed (every response says
    // so, and the server closes it once this returns). What the client has already sent is read first, briefly and
    // only up to a bound, so closing doesn't reset the connection before it reads the answer; the rest is never read.
    static void _refuse( SOK_T& conn, status_code status )
    {
        if( !conn.valid() )
            return;
        try
        {
            r_server_response response(status);
            response.write_response(conn, 1000);

            char discard[4096];
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            size_t drained = 0;
            while(drained < 65536)
            {
                auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
                if(left <= 0)
                    break;
                uint64_t wait_ms = (uint64_t)left;
                if(!conn.wait_till_recv_wont_block(wait_ms))
                    break;
                int n = conn.recv(discard, sizeof(discard));
                if(n <= 0)
                    break;
                drained += (size_t)n;
            }
        }
        catch(...)
        {
        }
    }

    size_t _maxBodySize {r_server_request::DEFAULT_MAX_BODY_SIZE};
    uint64_t _readDeadlineMs {DEFAULT_REQUEST_READ_DEADLINE_MS};
    std::map<int, std::map<std::string, http_cb>> _cbs;
    std::map<std::string, r_utils::r_blob_tree> _bundles;
    r_utils::r_server_threaded<SOK_T> _server;
    std::thread _serverThread;
};

}

#endif
