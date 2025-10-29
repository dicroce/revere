
#ifndef r_http_r_server_response_h
#define r_http_r_server_response_h

#include "r_http/r_status_codes.h"
#include "r_utils/interfaces/r_socket_base.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_macro.h"
#include <vector>
#include <list>
#include <map>

namespace r_http
{

class r_server_response
{
public:
    R_API r_server_response(status_code status = response_ok,
                      const std::string& contentType = "text/plain");

    R_API r_server_response(const r_server_response& obj);

    R_API virtual ~r_server_response() noexcept;

    R_API r_server_response& operator=(const r_server_response& obj);

    R_API void set_status_code(status_code status);
    R_API status_code get_status_code() const;

    void set_connection_close(bool connectionClose) { _connectionClose = connectionClose; }
    bool get_connection_close() const { return _connectionClose; }

//
// Possible Content-Type: values...
//     "text/plain"
//     "image/jpeg"
//     "application/xhtml+xml"
//     "application/x-rtsp-tunnelled"
//     "multipart/mixed; boundary="foo"

    R_API void set_content_type(const std::string& contentType);
    R_API std::string get_content_type() const;

    R_API void set_body(std::vector<uint8_t>&& body);
    R_API void set_body(const std::string& body);
    R_API void set_body( size_t bodySize, const void* bits );

    R_API size_t get_body_size() const;
    R_API const void* get_body() const;

    R_API std::string get_body_as_string() const;

    R_API void clear_additional_headers();
    R_API void add_additional_header(const std::string& headerName,
                               const std::string& headerValue);
    R_API std::string get_additional_header(const std::string& headerName);

    bool written() const { return _responseWritten; }

    R_API void write_response(r_utils::r_socket_base& socket);

    // Chunked transfer encoding support...
    R_API void write_chunk(r_utils::r_socket_base& socket, size_t sizeChunk, const void* bits);
    R_API void write_chunk_finalizer(r_utils::r_socket_base& socket);

    // Multipart mimetype support
    // WritePart() will automaticaly add a Content-Length header per
    // part.

    R_API void write_part( r_utils::r_socket_base& socket,
                     const std::string& boundary,
                     const std::map<std::string,std::string>& partHeaders,
                     void* chunk,
                     uint32_t size );

    R_API void write_part_finalizer(r_utils::r_socket_base& socket, const std::string& boundary);

private:
    std::string _get_status_message(status_code sc) const;
    bool _write_header(r_utils::r_socket_base& socket);

    status_code _status;
    bool _connectionClose;
    std::string _contentType;
    mutable std::vector<uint8_t> _body;
    bool _headerWritten;
    std::map<std::string,std::string> _additionalHeaders;
    bool _responseWritten;
};

}

#endif
