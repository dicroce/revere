
#include "r_http/r_server_response.h"
#include "r_http/r_http_exception.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"

#include <ctime>

using namespace r_http;
using namespace r_utils;
using namespace std;

static const uint64_t DEFAULT_SEND_TIMEOUT = 10000; // 10 seconds

r_server_response::r_server_response(status_code status, const string& contentType) :
    _status(status),
    _connectionClose(true),
    _contentType(contentType),
    _body(),
    _headerWritten(false),
    _additionalHeaders(),
    _responseWritten(false)
{
}

r_server_response::r_server_response(const r_server_response& obj) :
    _status(obj._status),
    _connectionClose(obj._connectionClose),
    _contentType(obj._contentType),
    _body(obj._body),
    _headerWritten(obj._headerWritten),
    _additionalHeaders(obj._additionalHeaders),
    _responseWritten(obj._responseWritten)
{
}

r_server_response::~r_server_response() noexcept
{
}

r_server_response& r_server_response::operator = (const r_server_response& obj)
{
    _status = obj._status;
    _connectionClose = obj._connectionClose;
    _contentType = obj._contentType;
    _body = obj._body;
    _headerWritten = obj._headerWritten;
    _additionalHeaders = obj._additionalHeaders;
    _responseWritten = obj._responseWritten;

    return *this;
}

void r_server_response::set_status_code(status_code status)
{
    _status = status;
}

status_code r_server_response::get_status_code() const
{
    return _status;
}

void r_server_response::set_content_type(const string& contentType)
{
    _contentType = contentType;
}

string r_server_response::get_content_type() const
{
    return _contentType;
}

void r_server_response::set_body(vector<uint8_t>&& body)
{
    _body = std::move(body);
}

void r_server_response::set_body(const string& body)
{
    set_body( body.length(), body.c_str() );
}

void r_server_response::set_body( size_t bodySize, const void* bits )
{
    _body.resize( bodySize );
    memcpy( &_body[0], bits, bodySize );
}

size_t r_server_response::get_body_size() const
{
    return _body.size();
}

const void* r_server_response::get_body() const
{
    return &_body[0];
}

string r_server_response::get_body_as_string() const
{
    return string((char*)&_body[0], _body.size());
}

void r_server_response::clear_additional_headers()
{
    _additionalHeaders.clear();
}

void r_server_response::add_additional_header(const string& headerName,
                                            const string& headerValue)
{
    auto found = _additionalHeaders.find( headerName );
    if( found != _additionalHeaders.end() )
        _additionalHeaders.erase(found);

    _additionalHeaders.insert( make_pair( headerName, headerValue) );
}

string r_server_response::get_additional_header(const string& headerName)
{
    auto found = _additionalHeaders.find( headerName );
    if( found != _additionalHeaders.end() )
        return (*found).second;

    return string();
}

void r_server_response::write_response(r_socket_base& socket)
{
    _responseWritten = true;

    time_t now = time(0);

#ifdef IS_LINUX
    char* cstr = ctime(&now);

    if( cstr == nullptr )
        R_STHROW(r_http_exception_generic, ("Please set Content-Type: before calling write_response()."));
#endif
#ifdef IS_WINDOWS
    char cstr[1024];
    memset(cstr, 0, 1024);
    if(ctime_s(cstr, 1024, &now) != 0)
        R_STHROW(r_http_exception_generic, ("Unable to get time string with ctime_s()."));
#endif

    // RStrip to remove \n added by ctime
    string timeString = r_string_utils::rstrip(string(cstr));

    if( (_body.size() > 0) && (_contentType.length() <= 0) )
        R_STHROW(r_http_exception_generic, ("Please set Content-Type: before calling write_response()."));

    string responseHeader = r_string_utils::format("HTTP/1.1 %d %s\r\nDate: %s\r\n",
                                             _status,
                                             _get_status_message(_status).c_str(),
                                             timeString.c_str() );

    if( _connectionClose )
        responseHeader += string("connection: close\r\n");

    if( _contentType.length() > 0 )
        responseHeader += r_string_utils::format( "Content-Type: %s\r\n",
                                            _contentType.c_str() );

    responseHeader += r_string_utils::format("Content-Length: %d\r\n", _body.size());

    auto i = _additionalHeaders.begin(), end = _additionalHeaders.end();
    while( i != end )
    {
        responseHeader += r_string_utils::format("%s: %s\r\n", (*i).first.c_str(), (*i).second.c_str());
        i++;
    }

    responseHeader += r_string_utils::format("\r\n");

    r_networking::r_send(socket, responseHeader.c_str(), responseHeader.length(), DEFAULT_SEND_TIMEOUT);
    if( !socket.valid() )
        R_STHROW( r_http_io_exception, ("Socket invalid."));

    if(_body.size() > 0)
    {
        r_networking::r_send(socket, &_body[0], _body.size(), DEFAULT_SEND_TIMEOUT);
        if( !socket.valid() )
            R_STHROW( r_http_io_exception, ("Socket invalid."));
    }
}

void r_server_response::write_chunk(r_socket_base& socket, size_t sizeChunk, const void* bits)
{
    _responseWritten = true;

    if(!_headerWritten)
        _write_header(socket);

    auto chunkSizeString = r_string_utils::format("%s;\r\n", r_string_utils::format("%x", (unsigned int)sizeChunk).c_str());
    r_networking::r_send(socket, chunkSizeString.c_str(), chunkSizeString.length(), DEFAULT_SEND_TIMEOUT);
    if( !socket.valid() )
        R_STHROW( r_http_io_exception, ("Socket invalid."));

    r_networking::r_send(socket, bits, sizeChunk, DEFAULT_SEND_TIMEOUT);
    if( !socket.valid() )
        R_STHROW( r_http_io_exception, ("Socket invalid."));

    string newLine("\r\n");
    r_networking::r_send(socket, newLine.c_str(), newLine.length(), DEFAULT_SEND_TIMEOUT);
    if( !socket.valid() )
        R_STHROW( r_http_io_exception, ("Socket invalid."));
}

void r_server_response::write_chunk_finalizer(r_socket_base& socket)
{
    string finalizer("0\r\n\r\n");
    r_networking::r_send(socket, finalizer.c_str(), finalizer.length(), DEFAULT_SEND_TIMEOUT);
    if( !socket.valid() )
        R_STHROW( r_http_io_exception, ("Socket invalid."));
}

void r_server_response::write_part(r_socket_base& socket,
                                   const string& boundary,
                                   const map<string,string>& partHeaders,
                                   void* chunk,
                                   uint32_t size)
{
    _responseWritten = true;

    auto boundaryLine = r_string_utils::format("--%s\r\n", boundary.c_str());
    r_networking::r_send(socket, boundaryLine.c_str(), boundaryLine.length(), DEFAULT_SEND_TIMEOUT);
    if( !socket.valid() )
        R_STHROW( r_http_io_exception, ("Socket invalid."));

    for( auto i = partHeaders.begin(); i != partHeaders.end(); i++ )
    {
        string headerName = (*i).first;
        string headerValue = (*i).second;
        string headerLine = r_string_utils::format("%s: %s\r\n",headerName.c_str(),headerValue.c_str());

        r_networking::r_send(socket, headerLine.c_str(), headerLine.length(), DEFAULT_SEND_TIMEOUT);
        if( !socket.valid() )
            R_STHROW( r_http_io_exception, ("Socket invalid."));
    }

    auto contentLength = r_string_utils::format("Content-Length: %d\r\n", size);
    r_networking::r_send(socket, contentLength.c_str(), contentLength.length(), DEFAULT_SEND_TIMEOUT);
    if( !socket.valid() )
        R_STHROW( r_http_io_exception, ("Socket invalid."));

    string newLine("\r\n");
    r_networking::r_send(socket, newLine.c_str(), newLine.length(), DEFAULT_SEND_TIMEOUT);
    if( !socket.valid() )
        R_STHROW( r_http_io_exception, ("Socket invalid."));

    r_networking::r_send(socket, chunk, size, DEFAULT_SEND_TIMEOUT);
    if( !socket.valid() )
        R_STHROW( r_http_io_exception, ("Socket invalid."));

    r_networking::r_send(socket, newLine.c_str(), newLine.length(), DEFAULT_SEND_TIMEOUT);
    if( !socket.valid() )
        R_STHROW( r_http_io_exception, ("Socket invalid."));
}

void r_server_response::write_part_finalizer(r_socket_base& socket, const string& boundary)
{
    auto finalizerLine = r_string_utils::format("--%s--\r\n", boundary.c_str());
    r_networking::r_send(socket, finalizerLine.c_str(), finalizerLine.length(), DEFAULT_SEND_TIMEOUT);
    if( !socket.valid() )
        R_STHROW( r_http_io_exception, ("Socket invalid."));
}

string r_server_response::_get_status_message(status_code sc) const
{
    switch(sc)
    {
    case response_continue:
        return string("Continue");

    case response_switching_protocols:
        return string("Switching Protocols");

    case response_ok:
        return string("OK");

    case response_created:
        return string("Created");

    case response_accepted:
        return string("Accepted");

    case response_no_content:
        return string("No Content");

    case response_reset_content:
        return string("Reset Content");

    case response_bad_request:
        return string("Bad Request");

    case response_unauthorized:
        return string("Unauthorized");

    case response_forbidden:
        return string("Forbidden");

    case response_not_found:
        return string("Not Found");

    case response_internal_server_error:
        return string("Internal Server Error");

    case response_not_implemented:
        return string("Not Implemented");

    default:
        break;
    };

    R_STHROW(r_http_exception_generic, ("Unknown status code."));
}

bool r_server_response::_write_header(r_socket_base& socket)
{
    time_t now = time(0);
#ifdef IS_LINUX
    char* cstr = ctime(&now);

    if( cstr == nullptr )
        R_STHROW(r_http_exception_generic, ("Please set Content-Type: before calling WriteResponse()."));
#endif
#ifdef IS_WINDOWS
    char cstr[1024];
    memset(cstr, 0, 1024);
    if(ctime_s(cstr, 1024, &now) != 0)
        R_STHROW(r_http_exception_generic, ("Unable to get time string with ctime_s()."));
#endif

    // RStrip to remove \n added by ctime
    string timeString = r_string_utils::rstrip(string(cstr));

    if(_contentType.length() <= 0)
        R_STHROW(r_http_exception_generic, ("Please set Content-Type: before calling WriteChunk()."));

    string responseHeader = r_string_utils::format("HTTP/1.1 %d %s\r\nDate: %s\r\nContent-Type: %s\r\nTransfer-Encoding: chunked\r\n",
                                             _status,
                                             _get_status_message(_status).c_str(),
                                             timeString.c_str(),
                                             _contentType.c_str());

    for( auto h : _additionalHeaders )
    {
        responseHeader += r_string_utils::format("%s: %s\r\n",h.first.c_str(), h.second.c_str());
    }

    responseHeader += r_string_utils::format("\r\n");

    r_networking::r_send(socket, responseHeader.c_str(), responseHeader.length(), DEFAULT_SEND_TIMEOUT);
    if( !socket.valid() )
        R_STHROW( r_http_io_exception, ("Socket invalid."));

    _headerWritten = true;

    return true;
}
