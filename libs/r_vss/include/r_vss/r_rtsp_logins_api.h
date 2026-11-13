#ifndef r_vss_r_rtsp_logins_api_h
#define r_vss_r_rtsp_logins_api_h

#include "r_vss/r_rtsp_logins.h"
#include "r_http/r_web_server.h"
#include "r_http/r_http_exception.h"
#include "r_http/r_server_request.h"
#include "r_http/r_server_response.h"
#include "r_utils/r_local_api_token.h"
#include "r_utils/3rdparty/json/json.h"
#include <functional>
#include <string>

namespace r_vss
{

// The web API for r_rtsp_logins, for processes on this machine (system plugins):
//
//   POST   /rtsp_logins {"name": "..."}   -> {"username", "password"}
//   GET    /rtsp_logins                   -> {"logins": [{"username", "name"}]}   (never the passwords)
//   DELETE /rtsp_logins/<username>        -> 204, 404 when there is no such login
//
// Only the local API token is accepted (a session token, which a browser or a person holds, is refused), and only
// from a loopback address, even on the HTTPS port that remote access opens to the network.
struct rtsp_logins_api
{
    // The bearer token of a request, or empty.
    std::function<std::string(const r_http::r_server_request&)> request_token;
    // Whether a token is the local API token.
    std::function<bool(const std::string&)> is_local_token;
};

// What every route requires of a caller: a loopback address, and the local token.
inline void require_local_caller(const std::string& peer_ip, const std::string& token, const rtsp_logins_api& api)
{
    using namespace r_http;
    // Failing either check is the same answer (403), so a caller learns nothing about which.
    if(!r_utils::r_local_api_token::is_loopback_host(peer_ip))
        R_STHROW(r_http_403_exception, ("RTSP logins are for this machine only."));
    if(token.empty())
        R_STHROW(r_http_401_exception, ("Unauthorized."));
    if(!api.is_local_token(token))
        R_STHROW(r_http_403_exception, ("The local API token is required."));
}

template<class SOK_T>
void add_rtsp_login_routes(r_http::r_web_server<SOK_T>& server, r_rtsp_logins& logins, rtsp_logins_api api)
{
    using namespace r_http;
    using json = nlohmann::json;
    static const std::string PATH = "/rtsp_logins";

    auto allowed = [api](SOK_T& conn, const r_server_request& request) {
        std::string peer;
        try { peer = conn.get_peer_ip(); } catch(...) {}
        require_local_caller(peer, api.request_token(request), api);
    };

    // The part of the path after /rtsp_logins: "" for the collection, the user name for one login.
    auto rest_of = [](const r_server_request& request) {
        auto path = request.get_uri().get_full_resource_path();
        if(path.compare(0, PATH.size(), PATH) != 0)
            R_STHROW(r_http_404_exception, ("Not found."));
        auto rest = path.substr(PATH.size());
        if(!rest.empty() && rest[0] != '/')
            R_STHROW(r_http_404_exception, ("Not found."));
        if(!rest.empty())
            rest = rest.substr(1);
        while(!rest.empty() && rest.back() == '/')
            rest.pop_back();
        return rest;
    };

    server.add_route(METHOD_POST, PATH, [&logins, allowed, rest_of](const r_web_server<SOK_T>&, SOK_T& conn, const r_server_request& request) {
        allowed(conn, request);
        if(!rest_of(request).empty())
            R_STHROW(r_http_404_exception, ("Not found."));
        std::string name;
        try
        {
            auto req = json::parse(request.get_body_as_string());
            if(!req.is_object() || !req.contains("name") || !req["name"].is_string())
                R_STHROW(r_http_400_exception, ("Missing name."));
            name = req["name"].get<std::string>();
        }
        catch(const json::exception&)
        {
            R_STHROW(r_http_400_exception, ("Expected a JSON body with a name."));
        }
        if(name.empty() || name.size() > r_rtsp_logins::MAX_NAME)
            R_STHROW(r_http_400_exception, ("The name must be 1 to %d bytes.", (int)r_rtsp_logins::MAX_NAME));
        auto created = logins.add(name);
        if(created.is_null())
            R_STHROW(r_http_403_exception, ("Too many RTSP logins (%d); remove one first.", (int)r_rtsp_logins::MAX_LOGINS));

        json j;
        j["username"] = created.value().username;
        j["password"] = created.value().password;
        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        response.add_additional_header("Cache-Control", "no-store");
        return response;
    });

    server.add_route(METHOD_GET, PATH, [&logins, allowed, rest_of](const r_web_server<SOK_T>&, SOK_T& conn, const r_server_request& request) {
        allowed(conn, request);
        if(!rest_of(request).empty())
            R_STHROW(r_http_404_exception, ("Not found."));
        json list = json::array();
        for(const auto& l : logins.list())
            list.push_back({{"username", l.username}, {"name", l.name}});
        json j;
        j["logins"] = list;
        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        response.add_additional_header("Cache-Control", "no-store");
        return response;
    });

    server.add_route(METHOD_DELETE, PATH, [&logins, allowed, rest_of](const r_web_server<SOK_T>&, SOK_T& conn, const r_server_request& request) {
        allowed(conn, request);
        auto username = rest_of(request);
        if(username.empty() || !logins.remove(username))
            R_STHROW(r_http_404_exception, ("No such login."));
        return r_server_response(response_no_content);
    });
}

}

#endif
