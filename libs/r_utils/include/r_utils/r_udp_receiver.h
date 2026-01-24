
#ifndef r_utils_r_udp_receiver_h
#define r_utils_r_udp_receiver_h

#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_socket_address.h"
#include <memory>
#include <list>

namespace r_utils
{

const int DEFAULT_UDP_RECV_BUF_SIZE = 0;

/// r_udp_receiver provides a thread-safe UDP socket receiver.
///
/// THREAD SAFETY:
/// The _associatedReceivers list is protected by a mutex to prevent
/// iterator invalidation when associate() or clear_associations() are
/// called concurrently with receive operations.
class r_udp_receiver final
{
public:
    R_API r_udp_receiver( int destinationPort = 0,
                    int recvBufferSize = DEFAULT_UDP_RECV_BUF_SIZE,
                    const std::string& destinationAddress = ip4_addr_any );

    R_API r_udp_receiver( const r_udp_receiver& ) = delete;
    R_API r_udp_receiver( r_udp_receiver&& obj ) = delete;

    R_API ~r_udp_receiver() noexcept {}

    R_API r_udp_receiver& operator = ( const r_udp_receiver& ) = delete;
    R_API r_udp_receiver& operator = ( r_udp_receiver&& obj ) = delete;

    R_API void connect( const std::string& ipAddress, int port );

    R_API virtual bool receive( int& port, std::vector<uint8_t>& buffer );

    R_API virtual bool receive( int& port, std::vector<uint8_t>& buffer, int waitMillis );

    R_API virtual bool non_blocking_receive( int& port, std::vector<uint8_t>& buffer );

    R_API virtual bool raw_receive( int& port, std::vector<uint8_t>& buffer );

    R_API virtual void shutdown();

    R_API virtual void close();

    R_API virtual void associate( std::shared_ptr<r_udp_receiver> receiver );

    R_API virtual void clear_associations();

    R_API virtual int get_bound_port();

    R_API virtual size_t get_recv_buffer_size();

    R_API virtual void set_recv_buffer_size( size_t size );

    R_API sock_t get_socket_fd() { return _sok; }

private:

    bool _receive( int& port, std::vector<uint8_t>& buffer, bool block, int waitMillis );

    sock_t _sok;
    r_socket_address _addr;
    mutable std::mutex _associatedReceiversLock;
    std::list<std::shared_ptr<r_udp_receiver> > _associatedReceivers;
};

}

#endif
