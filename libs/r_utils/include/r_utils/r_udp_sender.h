
#ifndef r_utils_r_udp_sender_h
#define r_utils_r_udp_sender_h

#include "r_utils/r_string_utils.h"
#include "r_utils/r_socket_address.h"
#include "r_utils/r_socket.h"
#include <vector>

#include <sys/types.h>

namespace r_utils
{

class r_udp_sender final
{
public:
    R_API r_udp_sender() = delete;
    R_API r_udp_sender( const r_udp_sender& ) = delete;
    R_API r_udp_sender( r_udp_sender&& ) = delete;
    R_API r_udp_sender(const std::string& targetIP,
                 int targetPort,
                 const std::string& localInterfaceIP = "",
                 int localPort = 0);

    R_API ~r_udp_sender() noexcept;

    R_API r_udp_sender& operator = ( const r_udp_sender& ) = delete;
    R_API r_udp_sender& operator = ( r_udp_sender&& ) = delete;

    R_API void aim( const std::string& targetIP, int targetPort );

    R_API int32_t send( void* buffer, size_t length );

    R_API virtual size_t get_send_buffer_size();

    R_API virtual void set_send_buffer_size(size_t size);

    R_API sock_t get_socket_fd() { return _sok; }

private:
    void _configure();
    void _close() throw();

    sock_t _sok;
    r_socket_address _addr;
    std::string _localInterfaceIP;
    int _localPort;
};

}

#endif
