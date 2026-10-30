
#include "browser_url.h"

using namespace std;

static const size_t MAX_URL_LENGTH = 2048;

static bool _starts_with_nocase(const string& s, const string& prefix)
{
    if(s.size() < prefix.size())
        return false;
    for(size_t i = 0; i < prefix.size(); ++i)
    {
        char c = s[i];
        if(c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if(c != prefix[i])
            return false;
    }
    return true;
}

bool revere::is_web_url(const string& url)
{
    if(url.empty() || url.size() > MAX_URL_LENGTH)
        return false;

    size_t rest;
    if(_starts_with_nocase(url, "https://"))
        rest = 8;
    else if(_starts_with_nocase(url, "http://"))
        rest = 7;
    else return false;

    for(unsigned char c : url)
    {
        if(c <= 0x20 || c == 0x7f || c == '"' || c == '\'' || c == '`' || c == '\\' || c == '<' || c == '>')
            return false;
    }

    // The host: everything up to the path, query or fragment, past any user info.
    auto end = url.find_first_of("/?#", rest);
    auto authority = url.substr(rest, (end == string::npos) ? string::npos : end - rest);
    auto at = authority.rfind('@');
    auto host = (at == string::npos) ? authority : authority.substr(at + 1);
    if(host.empty() || host[0] == ':')
        return false;

    return true;
}
