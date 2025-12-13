
#include "r_http/r_server_request.h"
#include "r_http/r_client_request.h"
#include "r_http/r_http_exception.h"
#include "r_http/r_utils.h"
#include "r_http/r_methods.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"
#include <sstream>
#include <algorithm>

using namespace r_http;
using namespace r_utils;
using namespace std;

static const uint64_t DEFAULT_RECV_TIMEOUT = 10000;
static const size_t MAX_HEADER_LINE = 16384;
static const size_t MAX_TOTAL_HEADERS_SIZE = 1024 * 1024; // 1MB limit for all headers
static const size_t MAX_BODY_SIZE = 100 * 1024 * 1024; // 100MB limit for request body

r_server_request::r_server_request() :
    _initialLine(),
    _headerParts(),
    _postVars(),
    _body(),
    _contentType(),
    _headerOverRead(),
    _chunkCallback(),
    _chunk()
{
}

r_server_request::r_server_request(const r_server_request& obj) :
    _initialLine(obj._initialLine),
    _headerParts(obj._headerParts),
    _postVars(obj._postVars),
    _body(obj._body),
    _contentType(obj._contentType),
    _headerOverRead(obj._headerOverRead),
    _chunkCallback(obj._chunkCallback),
    _chunk(obj._chunk)
{
}

r_server_request::~r_server_request() noexcept
{
}

r_server_request& r_server_request::operator = (const r_server_request& obj)
{
    _initialLine = obj._initialLine;
    _headerParts = obj._headerParts;
    _postVars = obj._postVars;
    _body = obj._body;
    _contentType = obj._contentType;
    _headerOverRead = obj._headerOverRead;
    _chunkCallback = obj._chunkCallback;
    _chunk = obj._chunk;

    return *this;
}

void r_server_request::read_request(r_utils::r_socket_base& socket, uint64_t timeout_millis)
{
    _headerOverRead.clear();

    string headerBlock = _read_headers(socket, timeout_millis);

    // Split header block into lines
    vector<string> lines = r_string_utils::split(headerBlock, "\n");

    if(lines.empty())
        R_STHROW(r_http_exception_generic, ("Empty HTTP request"));

    // First line is the initial/request line
    _initialLine = lines[0];
    if(!_initialLine.empty() && _initialLine.back() == '\r')
        _initialLine.pop_back();

    // Process remaining lines as headers
    list<string> requestLines;
    for(size_t i = 1; i < lines.size(); ++i)
    {
        string line = lines[i];
        // Remove trailing \r (from \r\n line endings after splitting on \n)
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        if(line.empty())
            continue;
        _add_line(requestLines, line);
    }

    _headerParts.clear();

    const vector<string> initialLineParts = r_string_utils::split(_initialLine, ' ');

    if(initialLineParts.size() != 3)
        R_STHROW(r_http_exception_generic, ("HTTP request initial line exceeded 3 parts"));

    _set_header("method", initialLineParts[0]);
    _set_header("uri", initialLineParts[1]);
    _set_header("http_version", r_string_utils::strip_eol(initialLineParts[2]));

    _process_request_lines(requestLines);

    const string method = r_string_utils::to_lower(initialLineParts[0]);
    if( method == r_string_utils::to_lower(method_text( METHOD_POST )) ||
        method == r_string_utils::to_lower(method_text( METHOD_PUT )) ||
        method == r_string_utils::to_lower(method_text( METHOD_PATCH )) ||
        method == r_string_utils::to_lower(method_text( METHOD_DELETE )) )
        _process_body(socket, timeout_millis);
}

bool r_server_request::is_patch_request() const
{
    return get_method() == METHOD_PATCH;
}

bool r_server_request::is_post_request() const
{
    return get_method() == METHOD_POST;
}

bool r_server_request::is_get_request() const
{
    return get_method() == METHOD_GET;
}

bool r_server_request::is_put_request() const
{
    return get_method() == METHOD_PUT;
}

bool r_server_request::is_delete_request() const
{
    return get_method() == METHOD_DELETE;
}

int r_server_request::get_method() const
{
    auto h = get_header("method");
    if(h.is_null())
        R_STHROW(r_http_exception_generic, ("request has no method."));
    
    return method_type(h.value());
}

r_uri r_server_request::get_uri() const
{
    auto h = get_header("uri");
    if(h.is_null())
        R_STHROW(r_http_exception_generic, ("request has no uri."));

    return r_uri(h.value());
}

string r_server_request::get_content_type() const
{
    return _contentType;
}

void r_server_request::_set_header(const string& name, const string& value)
{
    const string adjName = adjust_header_name(name);
    const string adjValue = adjust_header_value(value);

    _headerParts[adjName] = adjValue;
}

r_nullable<string> r_server_request::get_header( const std::string& key ) const
{
    r_nullable<string> result;

    auto found = _headerParts.find( r_string_utils::to_lower(key) );
    if( found != _headerParts.end() )
        result.set_value(found->second);

    return result;
}

map<string,string> r_server_request::get_headers() const
{
    return _headerParts;
}

const uint8_t* r_server_request::get_body() const
{
    return _body.empty() ? nullptr : _body.data();
}

size_t r_server_request::get_body_size() const
{
    return _body.size();
}

string r_server_request::get_body_as_string() const
{
    if(_body.empty())
        return string();
    return string((char*)_body.data(), _body.size());
}

map<string,string> r_server_request::get_post_vars() const
{
    return _postVars;
}

string r_server_request::_read_headers(r_socket_base& socket, uint64_t timeout_millis)
{
    static const size_t CHUNK_SIZE = 4096;
    string buffer;
    buffer.reserve(CHUNK_SIZE);

    char chunk[CHUNK_SIZE];

    while(buffer.size() < MAX_TOTAL_HEADERS_SIZE)
    {
        // Note: we do not use r_networking::r_recv() here because we are handling partial reads
        int received = socket.recv(chunk, CHUNK_SIZE);
        if(!socket.valid())
            R_STHROW(r_http_io_exception, ("Socket invalid."));

        if(received <= 0)
            R_STHROW(r_http_io_exception, ("Connection closed while reading headers."));

        buffer.append(chunk, received);

        // Look for end of headers: \r\n\r\n or \n\n
        size_t endPos = buffer.find("\r\n\r\n");
        size_t endLen = 4;
        if(endPos == string::npos)
        {
            endPos = buffer.find("\n\n");
            endLen = 2;
        }

        if(endPos != string::npos)
        {
            // Found end of headers - save any leftover bytes for body
            size_t headerEnd = endPos + endLen;
            if(headerEnd < buffer.size())
            {
                size_t leftoverSize = buffer.size() - headerEnd;
                _headerOverRead.resize(leftoverSize);
                memcpy(_headerOverRead.data(), buffer.data() + headerEnd, leftoverSize);
            }

            return buffer.substr(0, endPos);
        }
    }

    R_STHROW(r_http_exception_generic, ("HTTP headers exceeded maximum size."));
}

bool r_server_request::_add_line(std::list<string>& lines, const string& line)
{
    if(r_string_utils::starts_with(line, "\r\n") || r_string_utils::starts_with(line, "\n"))
        return true;

    if(r_string_utils::starts_with(line, " ") || r_string_utils::starts_with(line, "\t"))
    {
        if(!lines.empty())
            lines.back() += line;
        else
            R_STHROW(r_http_exception_generic, ("First line of header missing needed seperator."));
    }
    else
        lines.push_back(line);

    return false;
}

void r_server_request::_process_request_lines(const list<string>& requestLines)
{
    for(auto iter = requestLines.begin(), end = requestLines.end(); iter != end; ++iter)
    {
        const size_t firstColon = iter->find(':');

        if(firstColon != string::npos)
        {
            const string key = iter->substr(0, firstColon);
            const string val = firstColon + 1 < iter->size() ? iter->substr(firstColon + 1) : "";

            _set_header(key, r_string_utils::strip_eol(val));
        }
    }
}

void r_server_request::_process_body(r_socket_base& socket, uint64_t timeout_millis)
{
    // Check for chunked transfer encoding first
    auto te = get_header("Transfer-Encoding");
    if(!te.is_null() && r_string_utils::contains(r_string_utils::to_lower(te.value()), "chunked"))
    {
        _read_chunked_body(socket, timeout_millis);
        return;
    }

    auto cl = get_header("Content-Length");

    if(!cl.is_null())
    {
        auto contentLengthString = r_string_utils::strip(cl.value());
        uint32_t contentLength = r_string_utils::s_to_uint32(contentLengthString);

        if(!contentLength)
            return;

        _body.resize(contentLength);

        size_t totalReceived = 0;

        // First, use any leftover bytes from header reading
        if(!_headerOverRead.empty())
        {
            size_t toCopy = min(_headerOverRead.size(), (size_t)contentLength);
            memcpy(_body.data(), _headerOverRead.data(), toCopy);
            totalReceived = toCopy;
            _headerOverRead.clear();
        }

        // Read remaining bytes from socket if needed
        if(totalReceived < contentLength)
        {
            unsigned char* buffer = _body.data() + totalReceived;
            size_t remaining = contentLength - totalReceived;

            int received = r_networking::r_recv(socket, buffer, remaining, timeout_millis);
            if(received > 0)
                totalReceived += received;
        }

        if(totalReceived < contentLength)
            _body.resize(totalReceived);

        if(!socket.valid())
            R_STHROW(r_http_io_exception, ("Socket invalid."));

        auto ct = get_header("Content-Type");

        if(!ct.is_null())
        {
            _contentType = r_string_utils::lstrip(ct.value());

            if(r_string_utils::contains(_contentType, "x-www-form-urlencoded"))
            {
                string rawBody((char*)&_body[0], (int)_body.size());

                vector<string> parts = r_string_utils::split(rawBody, "&");

                for(size_t i = 0; i < parts.size(); ++i)
                {
                    string nvPair = parts[i];

                    vector<string> nameAndValue = r_string_utils::split(nvPair, "=");

                    if(nameAndValue.size() == 2)
                        _postVars.insert( make_pair( (string)nameAndValue[0], nameAndValue[1] ) );
                }
            }
        }
    }
}

void r_server_request::register_chunk_callback(server_chunk_callback cb)
{
    _chunkCallback = cb;
}

bool r_server_request::_read_line(r_socket_base& socket, string& line, uint64_t timeout_millis)
{
    line.clear();
    char ch;

    // First consume any leftover bytes from header reading
    while(!_headerOverRead.empty())
    {
        ch = _headerOverRead[0];
        _headerOverRead.erase(_headerOverRead.begin());

        if(ch == '\n')
            return true;
        if(ch != '\r')
            line += ch;
    }

    // Read from socket
    while(socket.valid())
    {
        int received = r_networking::r_recv(socket, &ch, 1, timeout_millis);
        if(received <= 0)
            return false;

        if(ch == '\n')
            return true;
        if(ch != '\r')
            line += ch;

        if(line.size() > MAX_HEADER_LINE)
            R_STHROW(r_http_exception_generic, ("Chunk size line too long."));
    }

    return false;
}

void r_server_request::_consume_footer(r_socket_base& socket, uint64_t timeout_millis)
{
    // After the final 0-size chunk, there's a trailing CRLF that terminates the chunked body.
    // Per HTTP/1.1 spec, there may also be trailer headers before this final CRLF, but they're rare.
    // We need to consume at least the final CRLF.
    string line;
    if(_read_line(socket, line, timeout_millis))
    {
        // If we got a non-empty line, it's a trailer header - keep reading until empty line
        while(!line.empty() && _read_line(socket, line, timeout_millis))
        {
            // Continue consuming trailer headers
        }
    }
}

void r_server_request::_read_chunked_body(r_socket_base& socket, uint64_t timeout_millis)
{
    string line;

    while(true)
    {
        // Read chunk size line
        if(!_read_line(socket, line, timeout_millis))
            R_STHROW(r_http_io_exception, ("Failed to read chunk size line."));

        // Strip any chunk extensions (everything after semicolon)
        size_t semicolonPos = line.find(';');
        if(semicolonPos != string::npos)
            line = line.substr(0, semicolonPos);

        // Parse hex chunk size
        uint32_t chunkLen = 0;
#ifdef IS_WINDOWS
        sscanf_s(line.c_str(), "%x", &chunkLen);
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
        sscanf(line.c_str(), "%x", &chunkLen);
#endif

        // Zero-length chunk signals end of body
        if(chunkLen == 0)
        {
            _consume_footer(socket, timeout_millis);
            return;
        }

        // Read chunk data
        _chunk.resize(chunkLen);
        size_t totalReceived = 0;

        // Use any leftover bytes first
        while(!_headerOverRead.empty() && totalReceived < chunkLen)
        {
            _chunk[totalReceived++] = _headerOverRead[0];
            _headerOverRead.erase(_headerOverRead.begin());
        }

        // Read remaining from socket - loop until all bytes received
        while(totalReceived < chunkLen && socket.valid())
        {
            int received = r_networking::r_recv(socket, &_chunk[totalReceived], chunkLen - totalReceived, timeout_millis);
            if(received <= 0)
                break;
            totalReceived += received;
        }

        if(totalReceived < chunkLen)
            R_STHROW(r_http_io_exception, ("Failed to read complete chunk data."));

        // Call callback if registered, otherwise accumulate
        if(_chunkCallback)
            _chunkCallback(_chunk, *this);
        else
        {
            size_t oldSize = _body.size();
            _body.resize(oldSize + _chunk.size());
            memcpy(&_body[oldSize], &_chunk[0], _chunk.size());
        }

        // Consume trailing CRLF after chunk data
        if(!_read_line(socket, line, timeout_millis))
            R_STHROW(r_http_io_exception, ("Failed to read chunk trailing line."));
    }
}
