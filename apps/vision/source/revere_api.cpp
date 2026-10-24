
#include "revere_api.h"
#include "utils.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_local_api_token.h"
#include "r_utils/r_logger.h"
#include "r_http/r_client_request.h"
#include <mutex>

using namespace vision;
using namespace r_utils;
using namespace std;

namespace
{
const int REVERE_HTTP_PORT = 8088;

mutex _session_lock;
string _session_token;

string _session()
{
    lock_guard<mutex> g(_session_lock);
    return _session_token;
}
}

int vision::revere_port(const string&)
{
    return REVERE_HTTP_PORT;
}

unique_ptr<r_socket_base> vision::revere_connect(const string& host, uint64_t io_timeout_ms)
{
    auto sok = make_unique<r_socket>();
    sok->set_io_timeout(io_timeout_ms);
    sok->connect(host, revere_port(host));
    return sok;
}

void vision::authorize(r_http::r_client_request& req, const string& host)
{
    // Read every time: it's a tiny file, and a Revere restart replaces it.
    if(r_local_api_token::is_loopback_host(host))
    {
        string dir;
        try { dir = top_dir(); } catch(...) {}
        auto local = r_local_api_token::read(dir);
        if(!local.is_null())
        {
            req.add_header("Authorization", r_local_api_token::bearer(local.value()));
            return;
        }
    }

    auto session = _session();
    if(!session.empty())
        req.add_header("Authorization", r_local_api_token::bearer(session));
}

void vision::set_session_token(const string& token)
{
    lock_guard<mutex> g(_session_lock);
    _session_token = token;
}
