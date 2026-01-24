
#include "r_http/r_utils.h"
#include "r_http/r_uri.h"
#include "r_http/r_http_exception.h"

using namespace r_utils;
using namespace r_http;
using namespace std;

void r_http::parse_url_parts( const string url, string& host, int& port, string& protocol, string& uri )
{
    // Initially set port based on protocol if present...
    if(r_string_utils::contains(url, "https://"))
    {
        protocol = "https";
        port = 443;
    }
    else
    {
        protocol = "http";
        port = 80;
    }

    size_t slashSlash = url.find("//");

    size_t urlStart = (slashSlash==string::npos)?0:slashSlash+2;

    size_t uriStart = url.find( "/", urlStart );

    if( uriStart != string::npos )
    {
        host = url.substr( urlStart, uriStart-urlStart );
        uri = url.substr( uriStart );
    }
    else
    {
        host = url.substr(urlStart);
        uri = "/";
    }

    // If our host part contains a colon, then override our earlier port number...
    if(r_string_utils::contains(host, ":"))
    {
        auto hostParts = r_string_utils::split(host, ":");
        port = r_string_utils::s_to_int(hostParts[1]);
        host = hostParts[0];
    }
}

string r_http::adjust_header_name( const string& name )
{
    for( size_t i = 0, e = name.size(); i < e; ++i )
    {
        const char c = name[i];
        if( c < 33 || c > 126 || c == ':' )
            R_STHROW( r_http_exception_generic, ("Invalid character in header value: [%c]", c) );
    }

    return r_string_utils::to_lower(name);
}

string r_http::adjust_header_value( const string& value )
{
    const size_t left = value.find_first_not_of(" \t");
    const size_t right = value.find_last_not_of(" \t");

    if( left == string::npos )
        return "";

    return value.substr( left, right - left + 1 );
}
