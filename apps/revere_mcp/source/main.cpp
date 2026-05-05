
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_http/r_methods.h"
#include "r_utils/r_socket.h"
#include "r_utils/3rdparty/json/json.h"
#include <iostream>
#include <string>

using namespace std;
using namespace r_http;
using namespace r_utils;
using json = nlohmann::json;

static const char* REVERE_HOST = "127.0.0.1";
static const int   REVERE_PORT = 10080;

int main()
{
    r_raw_socket::socket_startup();

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

            r_socket sock;
            sock.connect(REVERE_HOST, REVERE_PORT);

            r_client_request req(REVERE_HOST, REVERE_PORT);
            req.set_method(METHOD_POST);
            req.set_uri("/mcp");
            req.set_content_type("application/json");
            req.set_body(line);
            req.write_request(sock);

            r_client_response resp;
            resp.read_response(sock);

            if(!is_notification)
            {
                auto body = resp.get_body_as_string();
                if(!body.is_null())
                {
                    cout << body.value() << "\n";
                    cout.flush();
                }
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
