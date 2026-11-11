#include "tray_fetch.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_local_api_token.h"

using namespace std;
using namespace r_utils;
using namespace r_http;

revere::cameras_reply revere::fetch_cameras(const string& host, int port, const string& top_dir, int timeout_ms)
{
    cameras_reply reply;
    try
    {
        r_socket sok;
        sok.set_io_timeout(timeout_ms);
        sok.connect(host, port);

        r_client_request req(host, port);
        req.set_uri("/cameras");
        auto token = r_local_api_token::read(top_dir);
        if(!token.is_null())
            req.add_header("Authorization", r_local_api_token::bearer(token.value()));
        req.write_request(sok);

        r_client_response resp;
        resp.read_response(sok);
        reply.status = resp.get_status();
        if(reply.status == 200)
        {
            auto body = resp.get_body_as_string();
            if(!body.is_null())
                reply.body = body.value();
        }
    }
    catch(const exception&)
    {
        // No answer: status stays 0.
    }
    return reply;
}
