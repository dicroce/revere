
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

class r_ssl_socket : public r_socket_base
{
public:
    R_API r_ssl_socket(bool enable_auth = false);
    R_API ~r_ssl_socket();

    R_API virtual void connect(const std::string& host, int port);
    // STARTTLS (SMTP, RFC 3207): connect speaking plain text, and start TLS when the server says to. connect() is the
    // two at once.
    R_API void connect_plain(const std::string& host, int port);
    R_API void start_tls();
    R_API virtual void close();
    // The TCP connection only (r_socket_base::shutdown): mbedtls isn't touched, so another thread may call it while
    // this one is inside a TLS read.
    R_API virtual void shutdown() override { _sok.shutdown(); }

    R_API virtual int send(const void* buf, size_t len);
    R_API virtual int recv(void* buf, size_t len);

    R_API virtual bool valid() const;

    R_API virtual bool wait_till_recv_wont_block(uint64_t& millis) const override;
    R_API virtual bool wait_till_send_wont_block(uint64_t& millis) const override;
    R_API virtual bool peer_closed() const override;

    R_API void set_io_timeout( uint64_t ioTimeOut ) { _ioTimeOut = ioTimeOut; }

    inline std::string get_peer_ip() const override { return _sok.get_peer_ip(); }

    // The SHA-256 fingerprint of the certificate the server presented
    // (r_utils/r_tls_fingerprint.h form), for pinning when enable_auth is off.
    // Empty before a successful connect().
    R_API std::string peer_cert_fingerprint_sha256() const;
    inline std::string get_local_ip() const { return _sok.get_local_ip(); }

    // The underlying TCP socket, beneath TLS (as r_ssl_server_socket::get_sok_id()).
    inline sock_t get_sok_id() const { return _sok.get_sok_id(); }

private:
    // mbedtls' reads and writes of the connection (the context is the r_ssl_socket).
    static int _bio_send(void* ctx, const unsigned char* buf, size_t len);
    static int _bio_recv(void* ctx, unsigned char* buf, size_t len);

    r_raw_socket _sok;

    mutable mbedtls_ssl_context _ssl;
    mbedtls_ssl_config _conf;
    mbedtls_ctr_drbg_context _ctr_drbg;
    mbedtls_entropy_context _entropy;
    mbedtls_x509_crt _ca_cert;

    std::string _host;
    mutable bool _valid;
    bool _plain {false};   // connected, TLS not started yet
    uint64_t _ioTimeOut;
};

} // namespace r_utils

#endif
