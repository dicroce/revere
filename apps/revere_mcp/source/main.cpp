
// stdio <-> HTTP bridge for Revere's MCP endpoint: reads JSON-RPC requests on
// stdin, POSTs each to the local Revere's /mcp, writes the reply to stdout.
//
// Revere's API requires a token. The bridge uses the local API token Revere
// publishes in its data directory (r_utils/r_local_api_token.h), so it works
// with no setup when run as the same user as Revere. Overrides, for a Revere
// started with --data-dir or running as another user:
//   revere_mcp --data-dir <dir>      read the token from <dir>
//   REVERE_API_TOKEN=<token>         use this token as-is

#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_http/r_methods.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_local_api_token.h"
#include "r_utils/3rdparty/json/json.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace std;
using namespace r_http;
using namespace r_utils;
using json = nlohmann::json;

static const char* REVERE_HOST = "127.0.0.1";
static const int   REVERE_PORT = 8088;

// Re-read per request: Revere replaces the token each time it starts.
static r_nullable<string> _token(const string& data_dir)
{
    if(const char* env = getenv("REVERE_API_TOKEN"))
    {
        r_nullable<string> t;
        if(*env)
            t.set_value(env);
        return t;
    }
    return r_local_api_token::read(data_dir);
}

int main(int argc, char** argv)
{
    r_raw_socket::socket_startup();

    string data_dir = r_local_api_token::default_top_dir();
    for(int i = 1; i + 1 < argc; ++i)
    {
        if(string(argv[i]) == "--data-dir")
            data_dir = argv[++i];
    }

    string line;
    while(getline(cin, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        if(line.empty())
            continue;

        json id = nullptr;
        bool is_notification = false;
        try
        {
            auto parsed = json::parse(line);
            is_notification = !parsed.contains("id");
            id = parsed.value("id", json(nullptr));

            auto token = _token(data_dir);
            if(token.is_null())
                throw runtime_error("no API token: is Revere running as this user? (looked in " +
                                    r_local_api_token::path(data_dir) + "; see --data-dir / REVERE_API_TOKEN)");

            r_socket sock;
            sock.connect(REVERE_HOST, REVERE_PORT);

            r_client_request req(REVERE_HOST, REVERE_PORT);
            req.set_method(METHOD_POST);
            req.set_uri("/mcp");
            req.set_content_type("application/json");
            req.add_header("Authorization", r_local_api_token::bearer(token.value()));
            req.set_body(line);
            req.write_request(sock);

            if(is_notification)
                continue;                 // fire-and-forget: no response expected, don't block

            r_client_response resp;
            resp.read_response(sock);
            if(resp.get_status() == 401)
                throw runtime_error("Revere refused the API token (stale token file? restart Revere or check --data-dir)");
            auto body = resp.get_body_as_string();
            if(!body.is_null())
            {
                cout << body.value() << "\n";
                cout.flush();
            }
        }
        catch(const exception& ex)
        {
            if(!is_notification)
            {
                json err = {
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"error", {{"code", -32603}, {"message", string("revere unreachable: ") + ex.what()}}}
                };
                cout << err.dump() << "\n";
                cout.flush();
            }
        }
    }

    return 0;
}
