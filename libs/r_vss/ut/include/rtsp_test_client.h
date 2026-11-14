#ifndef rtsp_test_client_h
#define rtsp_test_client_h

// A bare RTSP client for the tests: one request on an open connection, and the Digest answer to a challenge.

#include "r_utils/r_md5.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"
#include <string>

namespace rtsp_test_client
{
using namespace r_utils;
using namespace std;

inline string _md5(const string& s)
{
    r_md5 h;
    h.update((const uint8_t*)s.data(), s.size());
    h.finalize();
    return h.get_as_string();
}

struct rtsp_reply
{
    int status {0};
    string www_authenticate;
};

// One request/response on an open RTSP connection (headers only; a body, if
// any, is drained by Content-Length).
inline rtsp_reply _request(r_socket& sok, const string& method, const string& url, int cseq, const string& extra_headers = string())
{
    string req = method + " " + url + " RTSP/1.0\r\nCSeq: " + to_string(cseq) + "\r\n" + extra_headers + "\r\n";
    r_networking::r_send(sok, req.data(), req.size(), 5000);

    string head;
    char c;
    while(head.size() < 16384 && head.find("\r\n\r\n") == string::npos)
    {
        if(r_networking::r_recv(sok, &c, 1, 5000) != 1)
            break;
        head.push_back(c);
    }

    rtsp_reply r;
    auto lines = r_string_utils::split(head, "\r\n");
    if(!lines.empty())
    {
        auto parts = r_string_utils::split(lines[0], ' ');
        if(parts.size() >= 2)
            r.status = r_string_utils::s_to_int(parts[1]);
    }
    size_t content_length = 0;
    for(auto& l : lines)
    {
        auto lower = r_string_utils::to_lower(l);
        if(lower.compare(0, 17, "www-authenticate:") == 0)
            r.www_authenticate = r_string_utils::strip(l.substr(17));
        if(lower.compare(0, 15, "content-length:") == 0)
            content_length = (size_t)r_string_utils::s_to_int(r_string_utils::strip(l.substr(15)));
    }
    if(content_length > 0)
    {
        string body(content_length, '\0');
        r_networking::r_recv(sok, &body[0], content_length, 5000);
    }
    return r;
}

// value of key="..." in a challenge.
inline string _param(const string& challenge, const string& key)
{
    auto k = key + "=\"";
    auto p = challenge.find(k);
    if(p == string::npos)
        return string();
    p += k.size();
    auto e = challenge.find('"', p);
    return e == string::npos ? string() : challenge.substr(p, e - p);
}

inline string _digest(const string& user, const string& pw, const string& realm, const string& nonce, const string& method, const string& uri)
{
    auto ha1 = _md5(user + ":" + realm + ":" + pw);
    auto ha2 = _md5(method + ":" + uri);
    auto response = _md5(ha1 + ":" + nonce + ":" + ha2);
    return "Authorization: Digest username=\"" + user + "\", realm=\"" + realm + "\", nonce=\"" + nonce +
           "\", uri=\"" + uri + "\", response=\"" + response + "\"\r\n";
}


}

#endif
