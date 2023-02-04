
#include "r_http/r_client_request.h"
#include "r_http/r_http_exception.h"
#include "r_http/r_utils.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"

#include <iostream>

using namespace r_http;
using namespace r_utils;
using namespace std;

static const unsigned int MAX_HEADER_LINE = 2048;

r_client_request::r_client_request( const std::string& host, int hostPort ) :
    _uri(),
    _acceptType(),
    _contentType(),
    _method( METHOD_GET ),
    _authData(),
    _postVars(),
    _headerParts(),
    _body(),
    _host( host ),
    _hostPort( hostPort )
{
}

r_client_request::r_client_request(const r_client_request& rhs) :
    _uri(rhs._uri),
    _acceptType(rhs._acceptType),
    _contentType(rhs._contentType),
    _method(rhs._method),
    _authData(rhs._authData),
    _postVars(rhs._postVars),
    _headerParts(rhs._headerParts),
    _body(rhs._body),
    _host(rhs._host),
    _hostPort(rhs._hostPort)
{
}

r_client_request::~r_client_request() noexcept
{
}

r_client_request& r_client_request::operator = (const r_client_request& rhs)
{
    if (this != &rhs)
    {
        _uri = rhs._uri;
        _acceptType = rhs._acceptType;
        _contentType = rhs._contentType;
        _method = rhs._method;
        _authData = rhs._authData;
        _postVars = rhs._postVars;
        _headerParts = rhs._headerParts;
        _body = rhs._body;
        _host = rhs._host;
        _hostPort = rhs._hostPort;
    }

    return *this;
}

void r_client_request::set_method( int method )
{
    _method = method;
}

void r_client_request::write_request( r_stream_io& socket ) const
{
    std::string msgHeader = _get_headers_as_string( socket );

    socket.send( msgHeader.c_str(), msgHeader.length() );
    if( !socket.valid() )
        R_STHROW( r_http_io_exception, ("Socket invalid."));

    if( (_method == METHOD_POST || _method == METHOD_PATCH || _method == METHOD_PUT) &&
        !r_string_utils::contains(_contentType, "x-www-form-urlencoded") && _body.size() )
    {
        socket.send(&_body[0], _body.size());
        if( !socket.valid() )
            R_STHROW( r_http_io_exception, ("Socket invalid."));
    }
}

void r_client_request::set_accept_type( const std::string& acceptType )
{
    _acceptType = acceptType;
}

void r_client_request::set_content_type(const std::string& contentType )
{
    _contentType = contentType;
}

void r_client_request::set_uri( const r_uri& res )
{
    _uri = res;
}

void r_client_request::set_basic_authentication( const std::string& user, const std::string& pass )
{
    const std::string auth = r_string_utils::format( "%s:%s", user.c_str(), pass.c_str() );

    _authData = r_string_utils::to_base64( auth.c_str(), auth.length() );
}

void r_client_request::add_post_var( const std::string& name, const std::string& value )
{
    _contentType = "x-www-form-urlencoded";
    _postVars[name] = value;
}

void r_client_request::add_header( const std::string& name, const std::string& value )
{
    _headerParts[adjust_header_name( name )] = adjust_header_value( value );
}

void r_client_request::set_body( const uint8_t* src, size_t size )
{
    _body.resize( size );
    memcpy( &_body[0], src, size );
}

void r_client_request::set_body( const std::string& body )
{
    set_body((uint8_t*)body.c_str(), body.length());
}

std::string r_client_request::_get_headers_as_string( r_stream_io& socket ) const
{
    std::string msgHeader;
    msgHeader = method_text( _method ) + " " + _uri.get_full_raw_uri() + " HTTP/1.1\r\nHost: " + _host + ":" + r_string_utils::int_to_s( _hostPort ) + "\r\n";

    if (!_acceptType.empty())
        msgHeader += "Accept: " + _acceptType + "\r\n";

    if (!_contentType.empty())
        msgHeader += "Content-Type: " + _contentType + "\r\n";

    if (!_authData.empty())
        msgHeader += "Authorization: Basic " + _authData + "\r\n";

    for( auto& kv : _headerParts )
    {
        msgHeader += r_string_utils::format( "%s: %s\r\n", kv.first.c_str(), kv.second.c_str() );
    }

    if( _method == METHOD_POST || _method == METHOD_PATCH || _method == METHOD_PUT )
    {
        if(r_string_utils::contains(_contentType,"x-www-form-urlencoded"))
        {
            // Post, with url-encoded nv pairs...

            std::string body;

            for( auto& kv : _postVars )
            {
                body += r_string_utils::format( "%s=%s&", kv.first.c_str(), kv.second.c_str() );
            }

            body = body.substr( 0, body.length() - 1 );

            msgHeader += r_string_utils::format( "Content-Length: %d\r\n\r\n", body.length() );
            msgHeader += body;
        }
        else
        {
            // Post, with an unknown body (this is the SOAP case)...

            if( _body.size() )
            {
                // If we have a body, add a Content-Length and write it...
                msgHeader += r_string_utils::format("Content-Length: %d\r\n\r\n", _body.size());
            }
            else
            {
                // No body in this request, so just write this...
                msgHeader += "\r\n";
            }
        }
    }
    else
    {
        // This is the GET case...

        msgHeader += "\r\n";
    }

    return msgHeader;
}
