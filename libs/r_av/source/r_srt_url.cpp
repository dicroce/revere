#include "r_av/r_srt_url.h"
#include "r_utils/r_exception.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

using namespace r_utils;
using namespace std;

namespace r_av
{

static string _lower(string s)
{
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)tolower(c); });
    return s;
}

static int _hex(char c)
{
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static string _decode(const string& s)
{
    string out;
    for(size_t i = 0; i < s.size(); ++i)
    {
        if(s[i] == '%')
        {
            if(i + 2 >= s.size())
                R_THROW(("Bad percent-encoding in an SRT URL."));
            int hi = _hex(s[i + 1]), lo = _hex(s[i + 2]);
            if(hi < 0 || lo < 0)
                R_THROW(("Bad percent-encoding in an SRT URL."));
            out.push_back((char)(hi * 16 + lo));
            i += 2;
        }
        else
            out.push_back(s[i]);
    }
    return out;
}

static int _int(const string& v, const char* what)
{
    if(v.empty() || v.size() > 9 || !all_of(v.begin(), v.end(), [](unsigned char c){ return isdigit(c) != 0; }))
        R_THROW(("The SRT URL's %s is not a number.", what));
    return atoi(v.c_str());
}

r_srt_endpoint r_parse_srt_url(const string& url)
{
    static const string scheme = "srt://";
    if(url.size() <= scheme.size() || _lower(url.substr(0, scheme.size())) != scheme)
        R_THROW(("Not an srt:// URL."));

    auto rest = url.substr(scheme.size());
    if(rest.find('#') != string::npos)
        R_THROW(("An SRT URL has no fragment."));

    auto q = rest.find('?');
    auto authority = rest.substr(0, q);
    auto query = q == string::npos ? string() : rest.substr(q + 1);

    if(authority.empty() || authority.find('@') != string::npos || authority.find('/') != string::npos)
        R_THROW(("An SRT URL is host[:port] only."));

    r_srt_endpoint e;
    auto colon = authority.rfind(':');
    if(colon == string::npos)
        e.host = authority;
    else
    {
        e.host = authority.substr(0, colon);
        auto port = _int(authority.substr(colon + 1), "port");
        if(port < 1 || port > 65535)
            R_THROW(("The SRT URL's port is out of range."));
        e.port = (uint16_t)port;
    }
    if(e.host.empty())
        R_THROW(("The SRT URL has no host."));

    size_t at = 0;
    while(at <= query.size() && !query.empty())
    {
        auto amp = query.find('&', at);
        auto item = query.substr(at, amp == string::npos ? string::npos : amp - at);
        at = amp == string::npos ? query.size() + 1 : amp + 1;
        if(item.empty())
            continue;
        auto eq = item.find('=');
        if(eq == string::npos)
            R_THROW(("An SRT URL option has no value."));
        auto name = _lower(item.substr(0, eq));
        auto value = _decode(item.substr(eq + 1));
        if(name == "streamid")
            e.stream_id = value;
        else if(name == "passphrase")
            e.passphrase = value;
        else if(name == "latency")
            e.latency_ms = _int(value, "latency") / 1000;
        else if(name == "pbkeylen")
            e.pbkeylen = _int(value, "pbkeylen");
        else if(name == "mode")
        {
            if(_lower(value) != "caller")
                R_THROW(("An SRT output connects as a caller only."));
        }
        else
            R_THROW(("An SRT URL option is not one a caller needs."));
    }
    return e;
}

}
