
#ifndef r_http_r_server_request_h
#define r_http_r_server_request_h

#include <list>
#include <map>
#include <memory>
#include <vector>
#include "r_utils/interfaces/r_socket_base.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include "r_http/r_uri.h"
#include "r_http/r_methods.h"

namespace r_http
{

class r_server_request
{
public:
    R_API r_server_request();

    R_API r_server_request(const r_server_request& rhs);

    R_API virtual ~r_server_request() noexcept;

    R_API r_server_request& operator = (const r_server_request& rhs);

    R_API void read_request(r_utils::r_socket_base& socket, uint64_t timeout_millis = 10000);

    R_API int get_method() const;

    R_API r_uri get_uri() const;

    R_API std::string get_content_type() const;

    R_API r_utils::r_nullable<std::string> get_header( const std::string& key ) const;
    R_API std::map<std::string,std::string> get_headers() const;

    R_API const uint8_t* get_body() const;
    R_API size_t get_body_size() const;
    R_API std::string get_body_as_string() const;

    R_API std::map<std::string,std::string> get_post_vars() const;

    R_API bool is_patch_request() const;
    R_API bool is_put_request() const;
    R_API bool is_post_request() const;
    R_API bool is_get_request() const;
    R_API bool is_delete_request() const;

private:
    void _set_header(const std::string& name, const std::string& value);
    std::string _read_headers(r_utils::r_socket_base& socket, uint64_t timeout_millis);
    bool _add_line(std::list<std::string>& lines, const std::string& line);
    void _process_request_lines(const std::list<std::string>& requestLines);
    void _process_body(r_utils::r_socket_base& socket, uint64_t timeout_millis);

    std::string _initialLine;
    std::map<std::string,std::string> _headerParts;
    std::map<std::string,std::string> _postVars;
    std::vector<uint8_t> _body;
    std::string _contentType;
    std::vector<uint8_t> _headerOverRead;
};

}

#endif
