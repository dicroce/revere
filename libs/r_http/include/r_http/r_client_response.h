
#ifndef r_http_r_client_response_h
#define r_http_r_client_response_h

#include <list>
#include <vector>
#include <map>
#include <functional>
#include "r_utils/r_macro.h"
#include "r_utils/interfaces/r_socket_base.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_nullable.h"

namespace r_http
{

class r_client_response;

typedef std::function<void(const std::vector<uint8_t>&, const r_client_response&)> chunk_callback;
typedef std::function<void(const std::vector<uint8_t>&, const std::map<std::string, std::string>&, const r_client_response&)> part_callback;

class r_client_response
{
public:
    R_API r_client_response();
    R_API r_client_response(const r_client_response& obj);
    R_API virtual ~r_client_response() noexcept;

    R_API r_client_response& operator = (const r_client_response& obj);

    R_API void read_response(r_utils::r_socket_base& socket, uint64_t timeout_millis = 10000);

    R_API std::string get_message();

    R_API void debug_print_request();

    R_API std::vector<uint8_t> release_body();

    R_API const void* get_body() const;

    R_API size_t get_body_size() const;

    R_API r_utils::r_nullable<std::string> get_body_as_string() const;

    R_API std::string get_header(const std::string& header) const;

    R_API std::vector<std::string> get_all_matching_headers(const std::string& header) const;

    R_API bool is_success() const;

    R_API bool is_failure() const;

    int get_status() const { return _statusCode; }

    R_API void register_chunk_callback( chunk_callback cb, bool streaming = false );
    R_API void register_part_callback( part_callback pb );

private:
    void _read_chunked_body(r_utils::r_socket_base& socket, uint64_t timeout_millis);
    void _read_multi_part(r_utils::r_socket_base& socket, uint64_t timeout_millis);
    std::map<std::string, std::string> _read_multi_header_lines(r_utils::r_socket_base& socket, char* lineBuf, uint64_t timeout_millis);
    void _read_end_of_line(r_utils::r_socket_base& socket, uint64_t timeout_millis);

    bool _is_legal_chunk_size_char(char ch) { return isxdigit(ch) ? true : false; } // VS warning: forcing int to bool.
    bool _embed_null(char* lineBuf);
    void _consume_footer(r_utils::r_socket_base& socket, uint64_t timeout_millis);

    void _add_header(const std::string& name, const std::string& value);

    void _clean_socket(r_utils::r_socket_base& socket, char** writer, uint64_t timeout_millis);
    std::string _read_headers(r_utils::r_socket_base& socket, uint64_t timeout_millis);
    void _read_header_line(r_utils::r_socket_base& socket, char* writer, bool firstLine, uint64_t timeout_millis);
    bool _add_line(std::list<std::string>& lines, const std::string& line);
    void _process_request_lines(const std::list<std::string>& requestLines);
    void _process_body(r_utils::r_socket_base& socket, uint64_t timeout_millis);

    bool _is_end_of_line(char* buffer)
    {
        return (buffer[0] == '\r' && buffer[1] == '\n') || buffer[0] == '\n';
    }

    std::string _initialLine;
    std::map<std::string, std::list<std::string> > _headerParts;
    std::vector<uint8_t> _bodyContents;
    bool _success;
    int  _statusCode;
    chunk_callback _chunkCallback;
    part_callback _partCallback;
    std::vector<uint8_t> _chunk;
    bool _streaming;
    std::vector<uint8_t> _headerOverRead;
};

}

#endif
