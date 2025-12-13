
#include "r_http/r_client_response.h"
#include "r_http/r_status_codes.h"
#include "r_http/r_http_exception.h"
#include "r_http/r_utils.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"

#include <iostream>

using namespace r_http;
using namespace r_utils;
using namespace std;

static const uint64_t DEFAULT_RECV_TIMEOUT = 10000; // 10 seconds

static const unsigned int MAX_HEADER_LINE = 2048;
static const size_t MAX_TOTAL_HEADERS_SIZE = 1024 * 1024; // 1MB limit for all headers

r_client_response::r_client_response() :
    _initialLine(),
    _headerParts(),
    _bodyContents(),
    _success(false),
    _statusCode(-1),
    _chunkCallback(),
    _partCallback(),
    _chunk(),
    _streaming(false),
    _headerOverRead()
{
}

r_client_response::r_client_response(const r_client_response& rhs)
    : _initialLine(rhs._initialLine),
      _headerParts(rhs._headerParts),
      _bodyContents(rhs._bodyContents),
      _success(rhs._success),
      _statusCode(rhs._statusCode),
      _chunkCallback(rhs._chunkCallback),
      _partCallback(rhs._partCallback),
      _chunk(rhs._chunk),
      _streaming(rhs._streaming),
      _headerOverRead(rhs._headerOverRead)
{
}

r_client_response::~r_client_response() noexcept
{
}

r_client_response& r_client_response::operator = (const r_client_response& rhs)
{
    if(&rhs != this)
    {
        _initialLine = rhs._initialLine;
        _headerParts = rhs._headerParts;
        _bodyContents = rhs._bodyContents;
        _success = rhs._success;
        _statusCode = rhs._statusCode;
        _chunkCallback = rhs._chunkCallback;
        _partCallback = rhs._partCallback;
        _chunk = rhs._chunk;
        _streaming = rhs._streaming;
        _headerOverRead = rhs._headerOverRead;
    }
    return *this;
}

void r_client_response::read_response(r_utils::r_socket_base& socket, uint64_t timeout_millis)
{
    _headerOverRead.clear();

    string headerBlock = _read_headers(socket, timeout_millis);

    // Split header block into lines
    vector<string> lines = r_string_utils::split(headerBlock, "\n");

    if(lines.empty())
        R_STHROW(r_http_exception_generic, ("Empty HTTP response"));

    // First line is the initial/status line
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

    /// Now, populate our header hash...

    _headerParts.clear();

PARSE_STATUS_LINE:
    {
        vector<string> initialLineParts = r_string_utils::split(_initialLine, ' ');

        if(initialLineParts.size() <= 2)
            R_STHROW(r_http_exception_generic, ("HTTP request initial line doesn't have enough parts."));

        _add_header(string("http_version"), initialLineParts[0]);
        _add_header(string("response_code"), initialLineParts[1]);

        // After response code, we have a message, usually either "OK" or "Not Found", this code appends all the initial line
        // pieces after the first two parts so that we end up with a complete "message".

        string msg = initialLineParts[2];

        for(int i = 3, e = (int)initialLineParts.size(); i < e; ++i)
        {
            msg += " ";
            msg += initialLineParts[i];
        }

        _add_header(string("message"), msg);

        auto i = _headerParts.find( "response_code" );
        if( i != _headerParts.end() )
        {
            _statusCode = r_string_utils::s_to_int(i->second.front());
            if(response_ok <= _statusCode && _statusCode < response_multiple_choices)
                _success = true;
        }

        // Handling a "100 continue" initial line, as per http 1.1 spec; we basically
        // just restart... A 100 continue means another complete header and body follows...
        // NOTE: We don't clear _headerOverRead here because it may contain bytes from the
        // real response that were read along with the 100 Continue response.
        if(r_string_utils::contains(r_string_utils::to_lower(msg), "continue"))
        {
            // Read next response headers, preserving any data already in _headerOverRead
            string headerBlock = _read_headers(socket, timeout_millis);
            lines = r_string_utils::split(headerBlock, "\n");
            if(lines.empty())
                R_STHROW(r_http_exception_generic, ("Empty HTTP response after 100 Continue"));
            _initialLine = lines[0];
            if(!_initialLine.empty() && _initialLine.back() == '\r')
                _initialLine.pop_back();
            requestLines.clear();
            for(size_t j = 1; j < lines.size(); ++j)
            {
                string line = lines[j];
                if(!line.empty() && line.back() == '\r')
                    line.pop_back();
                if(line.empty())
                    continue;
                _add_line(requestLines, line);
            }
            _headerParts.clear();
            goto PARSE_STATUS_LINE;
        }

        _process_request_lines(requestLines);
        _process_body(socket, timeout_millis);
    } // end of PARSE_STATUS_LINE scope
}

void r_client_response::_clean_socket(r_socket_base& socket, char** writer, uint64_t timeout_millis)
{
    if(!socket.valid())
        R_STHROW(r_http_exception_generic, ("Invalid Socket"));

    char tempBuffer[1];

    // Clear junk off the socket
    while(true)
    {
        r_networking::r_recv(socket, tempBuffer, 1, timeout_millis);

        if(!r_string_utils::is_space(tempBuffer[0]))
        {
            **writer = tempBuffer[0];
            ++*writer;
            break;
        }
    }
}

string r_client_response::_read_headers(r_socket_base& socket, uint64_t timeout_millis)
{
    static const size_t CHUNK_SIZE = 4096;
    string buffer;
    buffer.reserve(CHUNK_SIZE);

    // First, consume any existing _headerOverRead data (e.g., from 100 Continue handling)
    if(!_headerOverRead.empty())
    {
        buffer.append(reinterpret_cast<char*>(_headerOverRead.data()), _headerOverRead.size());
        _headerOverRead.clear();

        // Check if we already have complete headers
        size_t endPos = buffer.find("\r\n\r\n");
        size_t endLen = 4;
        if(endPos == string::npos)
        {
            endPos = buffer.find("\n\n");
            endLen = 2;
        }
        if(endPos != string::npos)
        {
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

    char chunk[CHUNK_SIZE];
    uint64_t remaining_millis = timeout_millis;

    while(buffer.size() < MAX_TOTAL_HEADERS_SIZE && remaining_millis > 0)
    {
        if(!socket.wait_till_recv_wont_block(remaining_millis))
            R_STHROW(r_http_exception_generic, ("Timeout while reading headers."));

        int received = socket.recv(chunk, CHUNK_SIZE);
        if(!socket.valid())
            R_STHROW(r_http_exception_generic, ("Socket invalid."));

        if(received < 0)
            R_STHROW(r_http_exception_generic, ("Connection closed while reading headers. received = %d", received));

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

bool r_client_response::_read_header_line(r_socket_base& socket, char* writer, bool firstLine, uint64_t timeout_millis)
{
    char lastTwoChars[2] = {0, 0};
    size_t bytesReadThisLine = 0;

    bool lineDone = false;
    while(!lineDone)
    {
        if(!socket.valid() && _headerOverRead.empty())
            return false;

        lastTwoChars[0] = lastTwoChars[1];

        // First consume any bytes from _headerOverRead
        if(!_headerOverRead.empty())
        {
            *writer = static_cast<char>(_headerOverRead[0]);
            _headerOverRead.erase(_headerOverRead.begin());
        }
        else
        {
            // Use r_recv which properly handles timeout - catch its exception and return false
            try {
                int received = r_networking::r_recv(socket, writer, 1, timeout_millis);
                if(received <= 0)
                    return false;
            } catch(const r_utils::r_socket_exception&) {
                // Timeout or socket error - return false to indicate read failed
                return false;
            }
        }

        ++bytesReadThisLine;
        if(bytesReadThisLine > MAX_HEADER_LINE)
            R_STHROW(r_http_exception_generic, ("Header line too long."));
        lastTwoChars[1] = *writer;
        if(lastTwoChars[0] == '\r' && lastTwoChars[1] == '\n')
            lineDone = true;
        else ++writer;
    }
    return true;
}

bool r_client_response::_add_line(std::list<string>& lines, const string& line)
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

void r_client_response::_process_request_lines(const list<string>& requestLines)
{
    // Now, iterate on the header lines...

    for(list<string>::const_iterator iter = requestLines.begin(), end = requestLines.end();
        iter != end;
        ++iter)
    {
        const size_t firstColon = iter->find(':');

        if(firstColon != string::npos)
        {
            const string key = iter->substr(0, firstColon);
            const string val = firstColon + 1 < iter->size() ? iter->substr(firstColon + 1) : "";

            _add_header(r_string_utils::to_lower(key), r_string_utils::strip_eol(val));
        }
    }
}

void r_client_response::_process_body(r_socket_base& socket, uint64_t timeout_millis)
{
    /// Get the body if we were given a Content Length
    auto found = _headerParts.find( "content-length" );
    if( found != _headerParts.end() )
    {
        const uint32_t contentLength = r_string_utils::s_to_uint32((*found).second.front());

        if(contentLength > 0)
        {
            _bodyContents.resize( contentLength );

            size_t totalReceived = 0;

            // First, use any leftover bytes from header reading
            if(!_headerOverRead.empty())
            {
                size_t toCopy = min(_headerOverRead.size(), (size_t)contentLength);
                memcpy(_bodyContents.data(), _headerOverRead.data(), toCopy);
                totalReceived = toCopy;
                _headerOverRead.clear();
            }

            // Read remaining bytes from socket if needed
            if(totalReceived < contentLength)
            {
                size_t remaining = contentLength - totalReceived;
                int received = r_networking::r_recv(socket, &_bodyContents[totalReceived], remaining, timeout_millis);
                if(received > 0)
                    totalReceived += received;
            }

            if(totalReceived < contentLength)
                _bodyContents.resize(totalReceived);
        }
    }
    else if( (found = _headerParts.find( "transfer-encoding" )) != _headerParts.end() )
    {
        if(r_string_utils::contains(r_string_utils::to_lower((*found).second.front()), "chunked"))
        {
            _read_chunked_body(socket, timeout_millis);
            return;
        }
    }

    if( (found = _headerParts.find("content-type")) != _headerParts.end() )
    {
        if(r_string_utils::contains(r_string_utils::to_lower((*found).second.front()), "multipart"))
        {
            // Don't clear _headerOverRead - it may contain the start of multipart body
            _read_multi_part(socket, timeout_millis);
            return;
        }
    }

    if(r_string_utils::contains(r_string_utils::to_lower(get_header("content-type")), "multipart"))
    {
        // Don't clear _headerOverRead - it may contain the start of multipart body
        _read_multi_part(socket, timeout_millis);
    }
}

vector<uint8_t> r_client_response::release_body()
{
    return std::move(_bodyContents);
}

const void* r_client_response::get_body() const
{
    if(_bodyContents.empty())
        return nullptr;
    return &_bodyContents[0];
}

size_t r_client_response::get_body_size() const
{
    return _bodyContents.size();
}

r_nullable<string> r_client_response::get_body_as_string() const
{
    r_nullable<string> output;
    if(!_bodyContents.empty())
        output.set_value(string((char*)&_bodyContents[0], _bodyContents.size()));
    return output;
}

string r_client_response::get_header(const string& name) const
{
    auto values = _headerParts.find( r_string_utils::to_lower(name) );

    return (values != _headerParts.end()) ? (*values).second.front() : "";
}

vector<string> r_client_response::get_all_matching_headers(const string& header) const
{
    vector<string> matchingHeaders;

    auto matches = _headerParts.find(r_string_utils::to_lower(header));
    if( matches != _headerParts.end() )
    {
        for( auto i = (*matches).second.begin(); i != (*matches).second.end(); ++i )
            matchingHeaders.push_back( *i );
    }

    return matchingHeaders;
}

string r_client_response::get_message()
{
    return get_header("message");
}

bool r_client_response::is_success() const
{
    return _success;
}

bool r_client_response::is_failure() const
{
    return !_success;
}

void r_client_response::register_chunk_callback( chunk_callback cb, bool streaming )
{
    _chunkCallback = cb;
    _streaming = streaming;
}

void r_client_response::register_part_callback( part_callback pb )
{
    _partCallback = pb;
}

void r_client_response::_read_chunked_body(r_socket_base& socket, uint64_t timeout_millis)
{
    string line;

    while(true)
    {
        // Read chunk size line, first consuming any leftover bytes from header reading
        line.clear();
        char ch;

        // Consume from _headerOverRead first
        while(!_headerOverRead.empty())
        {
            ch = _headerOverRead[0];
            _headerOverRead.erase(_headerOverRead.begin());

            if(ch == '\n')
                goto parse_chunk_size;
            if(ch != '\r')
                line += ch;
        }

        // Read from socket
        while(socket.valid())
        {
            int received = r_networking::r_recv(socket, &ch, 1, timeout_millis);
            if(received <= 0)
                R_STHROW(r_http_io_exception, ("Failed to read chunk size line."));

            if(ch == '\n')
                goto parse_chunk_size;
            if(ch != '\r')
                line += ch;

            if(line.size() > MAX_HEADER_LINE)
                R_STHROW(r_http_exception_generic, ("Chunk size line too long."));
        }

        R_STHROW(r_http_io_exception, ("Socket closed while reading chunk size."));

parse_chunk_size:
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

        // Read remaining from socket - loop until we get all bytes
        while(totalReceived < chunkLen)
        {
            int received = r_networking::r_recv(socket, &_chunk[totalReceived], chunkLen - totalReceived, timeout_millis);
            if(received <= 0)
                break;
            totalReceived += received;
        }

        if(totalReceived < chunkLen)
            _chunk.resize(totalReceived);

        // Call callback if registered
        if(_chunkCallback)
            _chunkCallback(_chunk, *this);

        // We only append a chunk to our "_bodyContents" if we are not streaming
        // (because "streams" potentially have no end, so a body that contains the
        // complete body contents would just grow forever).
        if(!_streaming)
        {
            size_t oldSize = _bodyContents.size();
            _bodyContents.resize(oldSize + _chunk.size());
            memcpy(&_bodyContents[oldSize], &_chunk[0], _chunk.size());
        }

        // Consume trailing CRLF after chunk data
        bool gotCRLF = false;

        // First from _headerOverRead
        while(!_headerOverRead.empty() && !gotCRLF)
        {
            ch = _headerOverRead[0];
            _headerOverRead.erase(_headerOverRead.begin());
            if(ch == '\n')
                gotCRLF = true;
        }

        // Then from socket if needed
        while(!gotCRLF && socket.valid())
        {
            int received = r_networking::r_recv(socket, &ch, 1, timeout_millis);
            if(received <= 0)
                R_STHROW(r_http_io_exception, ("Failed to read chunk trailing CRLF."));
            if(ch == '\n')
                gotCRLF = true;
        }
    }
}

bool r_client_response::_embed_null(char* lineBuf)
{
    // Scan forward in the chunk header line and embed a nullptr on the first non legal chunk size char.
    for(size_t i = 0; i < MAX_HEADER_LINE; ++i)
    {
        if(!_is_legal_chunk_size_char(lineBuf[i]))
        {
            lineBuf[i] = 0;
            return true;
        }
    }

    return false;
}

void r_client_response::_read_multi_part(r_socket_base& socket, uint64_t timeout_millis)
{
    char lineBuf[MAX_HEADER_LINE+1];
    bool moreParts = true;

    while(moreParts)
    {
        // First, read the boundary line...
        memset(lineBuf, 0, MAX_HEADER_LINE);

        if(!_read_header_line(socket, lineBuf, false, timeout_millis))
            break; // Socket closed or timeout - done reading

        if(r_string_utils::ends_with(r_string_utils::strip_eol(string(lineBuf)), "--"))
            break;

        map<string,string> partHeaders = _read_multi_header_lines(socket, lineBuf, timeout_millis);
        if(partHeaders.empty() && !socket.valid())
            break; // Socket closed during header reading

        auto i = partHeaders.find( "content-length" );
        if( i != partHeaders.end() )
        {
            const int partContentLength = r_string_utils::s_to_int((*i).second);

            _chunk.resize(partContentLength);

            // Loop until all bytes received
            size_t totalReceived = 0;

            // First consume any bytes from _headerOverRead
            while(!_headerOverRead.empty() && totalReceived < (size_t)partContentLength)
            {
                _chunk[totalReceived++] = _headerOverRead[0];
                _headerOverRead.erase(_headerOverRead.begin());
            }

            // Read remaining from socket
            while(totalReceived < (size_t)partContentLength && socket.valid())
            {
                // Use direct socket operations to avoid r_recv throwing exceptions
                if(!socket.wait_till_recv_wont_block(timeout_millis))
                    break; // Timeout - stop reading

                int received = socket.recv(&_chunk[totalReceived], partContentLength - totalReceived);
                if(received <= 0)
                    break;
                totalReceived += received;
            }
            if(totalReceived < (size_t)partContentLength)
                _chunk.resize(totalReceived);

            // call callback here...
            if( _partCallback )
                _partCallback( _chunk, partHeaders, *this );

            _read_end_of_line(socket, timeout_millis);
        }
        else if(!partHeaders.empty())
            R_STHROW(r_http_exception_generic, ("Oops. Mime multipart without a Content-Length!"));
    }
}

void r_client_response::_read_end_of_line(r_socket_base& socket, uint64_t timeout_millis)
{
    char lineEnd[2] = {0, 0};

    // First try to read from _headerOverRead
    if(!_headerOverRead.empty())
    {
        lineEnd[0] = static_cast<char>(_headerOverRead[0]);
        _headerOverRead.erase(_headerOverRead.begin());
    }
    else
    {
        if(!socket.valid())
            return;

        if(!socket.wait_till_recv_wont_block(timeout_millis))
            return; // Timeout - acceptable at end of multipart

        int received = socket.recv(&lineEnd[0], 1);
        if(received <= 0)
            return;
    }

    if(lineEnd[0] == '\r')
    {
        // Read the \n
        if(!_headerOverRead.empty())
        {
            _headerOverRead.erase(_headerOverRead.begin());
        }
        else if(socket.valid())
        {
            if(!socket.wait_till_recv_wont_block(timeout_millis))
                return;
            socket.recv(&lineEnd[1], 1);
        }
    }

    // Don't throw on invalid line ending - just return
}

map<string,string> r_client_response::_read_multi_header_lines(r_socket_base& socket, char* lineBuf, uint64_t timeout_millis)
{
    std::list<string> partLines;

    /// Now, read the rest of the header lines...
    while(true)
    {
        memset(lineBuf, 0, MAX_HEADER_LINE);

        if(!_read_header_line(socket, lineBuf, false, timeout_millis))
            break; // Socket closed or timeout

        if(_add_line(partLines, lineBuf))
            break;
    }

    map<string,string> partHeaders;

    for(auto iter = partLines.begin(), end = partLines.end(); iter != end; ++iter)
    {
        auto lineParts = r_string_utils::split(*iter, ':');
        if(lineParts.size() >= 2)
            partHeaders.insert( make_pair( adjust_header_name( lineParts[0] ), adjust_header_value( r_string_utils::strip_eol(lineParts[1]) ) ) );
    }

    return partHeaders;
}

void r_client_response::debug_print_request()
{
    string method = get_header(string("method"));
    string response_code = get_header(string("response_code"));
    string http_version = get_header(string("http_version"));
    printf("method = %s\n", method.c_str());
    printf("response_code = %s\n", response_code.c_str());
    printf("http_version = %s\n", http_version.c_str());
    fflush(stdout);
}

void r_client_response::_consume_footer(r_socket_base& socket, uint64_t timeout_millis)
{
    // After the final 0-size chunk, there's a trailing CRLF that terminates the chunked body.
    // Per HTTP/1.1 spec, there may also be trailer headers before this final CRLF, but they're rare.
    string line;
    char ch;

    while(true)
    {
        line.clear();

        // Read a line, first from _headerOverRead then from socket
        bool gotLine = false;

        while(!_headerOverRead.empty())
        {
            ch = _headerOverRead[0];
            _headerOverRead.erase(_headerOverRead.begin());

            if(ch == '\n')
            {
                gotLine = true;
                break;
            }
            if(ch != '\r')
                line += ch;
        }

        if(!gotLine)
        {
            while(socket.valid())
            {
                int received = r_networking::r_recv(socket, &ch, 1, timeout_millis);
                if(received <= 0)
                    return; // Socket closed or timeout - acceptable for footer

                if(ch == '\n')
                {
                    gotLine = true;
                    break;
                }
                if(ch != '\r')
                    line += ch;
            }
        }

        // Empty line signals end of footer
        if(line.empty())
            return;
    }
}

void r_client_response::_add_header(const string& name, const string& value)
{
    const string adjName = adjust_header_name(name);
    const string adjValue = adjust_header_value(value);

    auto found = _headerParts.find( adjName );
    if( found != _headerParts.end() )
        (*found).second.push_back( adjValue );
    else
    {
        list<string> values;
        values.push_back(adjValue);
        _headerParts.insert( make_pair( adjName, values ) );
    }
}
