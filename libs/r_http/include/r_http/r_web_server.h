
#ifndef r_http_r_web_server_h
#define r_http_r_web_server_h

#include "r_http/r_server_request.h"
#include "r_http/r_server_response.h"
#include "r_http/r_methods.h"
#include "r_http/r_status_codes.h"
#include "r_http/r_http_exception.h"
#include "r_utils/r_server_threaded.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_macro.h"
#include "r_utils/r_blob_tree.h"
#include <functional>
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

namespace r_http
{

template<class SOK_T>
class r_web_server;

template<class SOK_T>
class r_web_server final
{
public:
    typedef std::function<r_server_response(const r_web_server<SOK_T>& ws, SOK_T& conn, const r_server_request& request)> http_cb;

    r_web_server(int port, const std::string& sockAddr = std::string()) :
        _cbs(),
        _server(port, std::bind(&r_web_server<SOK_T>::_server_conn_cb, this, std::placeholders::_1), sockAddr),
        _serverThread()
    {
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

    void stop()
    {
        if( _server.started() )
        {
            _server.stop();
            _serverThread.join();
        }
    }

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
        request.read_request(conn);

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
        WS_CATCH(r_http_400_exception, response_bad_request)
        WS_CATCH(r_http_401_exception, response_unauthorized)
        WS_CATCH(r_http_403_exception, response_forbidden)
        WS_CATCH(r_http_404_exception, response_not_found)
        WS_CATCH(r_http_500_exception, response_internal_server_error)
        WS_CATCH(r_http_501_exception, response_not_implemented)
        WS_CATCH(r_utils::r_not_found_exception, response_not_found)
        WS_CATCH(r_utils::r_invalid_argument_exception, response_bad_request)
        WS_CATCH(r_utils::r_unauthorized_exception, response_forbidden)
        WS_CATCH(r_utils::r_not_implemented_exception, response_not_implemented)
        WS_CATCH(r_utils::r_timeout_exception, response_internal_server_error)
        WS_CATCH(r_utils::r_io_exception, response_internal_server_error)
        WS_CATCH(r_utils::r_internal_exception, response_internal_server_error)
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

    std::map<int, std::map<std::string, http_cb>> _cbs;
    std::map<std::string, r_utils::r_blob_tree> _bundles;
    r_utils::r_server_threaded<SOK_T> _server;
    std::thread _serverThread;
};

}

#endif
