
#ifndef __revere_ws_h
#define __revere_ws_h

#include "r_http/r_web_server.h"
#include "r_http/r_http_exception.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_macro.h"
#include "r_disco/r_devices.h"
#include <vector>

class r_ws final
{
public:
    R_API r_ws(const std::string& top_dir, r_disco::r_devices& devices);
    R_API ~r_ws();

    R_API std::vector<uint8_t> get_jpg(const std::string& camera_id, int64_t ts, uint16_t w, uint16_t h);

    R_API std::vector<uint8_t> get_key_frame(const std::string& camera_id, int64_t ts);

private:
    r_http::r_server_response _get_jpg(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                       r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                       const r_http::r_server_request& request);

    r_http::r_server_response _get_key_frame(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                             r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                             const r_http::r_server_request& request);

    r_http::r_server_response _get_contents(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                            r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                            const r_http::r_server_request& request);

    r_http::r_server_response _get_cameras(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                           r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                           const r_http::r_server_request& request);

    r_http::r_server_response _get_export(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                          r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                          const r_http::r_server_request& request);

    r_http::r_server_response _get_motions(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                           r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                           const r_http::r_server_request& request);

    r_http::r_server_response _get_motion_events(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                                 r_utils::r_buffered_socket<r_utils::r_socket>& conn,
                                                 const r_http::r_server_request& request);

    std::string _top_dir;
    r_disco::r_devices& _devices;
    r_http::r_web_server<r_utils::r_socket> _server;
};

#endif
