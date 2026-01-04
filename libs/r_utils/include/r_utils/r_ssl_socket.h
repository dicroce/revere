
#ifndef r_utils_r_ssl_socket_h
#define r_utils_r_ssl_socket_h

extern "C" {
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
//#include <mbedtls/ssl_internal.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include <mbedtls/debug.h>
}

#include "r_utils/interfaces/r_socket_base.h"
#include "r_utils/r_socket_address.h"
#include "r_utils/r_socket.h"
#include <string>
#include <mutex>

namespace r_utils {

/// r_ssl_socket provides a thread-safe TLS socket wrapper using mbedtls.
///
/// THREAD SAFETY:
/// All SSL operations are protected by a mutex to prevent use-after-free
/// when close() is called while send/recv callbacks are in progress.
/// The mbedtls callbacks access the underlying socket through a pointer,
/// so we must ensure the socket remains valid during callback execution.
class r_ssl_socket : public r_socket_base
{
public:
    R_API r_ssl_socket(bool enable_auth = false);
    R_API ~r_ssl_socket();

    R_API virtual void connect(const std::string& host, int port);
    R_API virtual void close() const;

    R_API virtual int send(const void* buf, size_t len);
    R_API virtual int recv(void* buf, size_t len);

    R_API virtual bool valid() const;

    R_API virtual bool wait_till_recv_wont_block(uint64_t& millis) const override;
    R_API virtual bool wait_till_send_wont_block(uint64_t& millis) const override;

    R_API void set_io_timeout( uint64_t ioTimeOut ) { _ioTimeOut = ioTimeOut; }

    inline std::string get_peer_ip() const { return _sok.get_peer_ip(); }
    inline std::string get_local_ip() const { return _sok.get_local_ip(); }

private:
    mutable std::recursive_mutex _sslLock;  // Protects SSL operations from concurrent access
    r_raw_socket _sok;

    mutable mbedtls_ssl_context _ssl;
    mbedtls_ssl_config _conf;
    mbedtls_ctr_drbg_context _ctr_drbg;
    mbedtls_entropy_context _entropy;
    mbedtls_x509_crt _ca_cert;

    std::string _host;
    mutable bool _valid;
    uint64_t _ioTimeOut;
};

} // namespace r_utils

#endif
