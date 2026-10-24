#ifndef r_utils_r_ssl_server_socket_h
#define r_utils_r_ssl_server_socket_h

extern "C" {
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
}

#include "r_utils/interfaces/r_socket_base.h"
#include "r_utils/r_socket.h"
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <utility>

namespace r_utils
{

// Everything a TLS server shares across its accepted connections: the
// certificate, the private key, the mbedTLS config built from them, and one
// mutex-guarded RNG. Build one, hand it (as a shared_ptr) to the listening
// r_ssl_server_socket; every accepted connection keeps a reference.
//
// Read-only after construction, so it may be used from any number of threads.
class r_ssl_server_config
{
public:
    // cert_pem may be a chain (leaf first). key_pem is the leaf's unencrypted
    // private key. Throws if either fails to parse or they don't match.
    R_API r_ssl_server_config(const std::string& cert_pem, const std::string& key_pem);
    R_API ~r_ssl_server_config();

    r_ssl_server_config(const r_ssl_server_config&) = delete;
    r_ssl_server_config& operator=(const r_ssl_server_config&) = delete;

    // Mint a fresh ECDSA P-256 self-signed certificate. Returns {cert_pem, key_pem},
    // ready to persist and feed back to the constructor. The certificate carries
    // CN=common_name plus SANs for "localhost" and 127.0.0.1 so a browser that
    // has been told to trust it is happy on the local machine.
    R_API static std::pair<std::string, std::string> generate_self_signed(const std::string& common_name = "revere",
                                                                          int valid_days = 3650);

    // SHA-256 of the leaf certificate's DER encoding as "AB:CD:...": the value a
    // client pins (and what Firefox/openssl display).
    R_API std::string fingerprint_sha256() const;

    R_API std::string cert_pem() const { return _cert_pem; }

    // Internal: the config accepted connections are set up against.
    mbedtls_ssl_config* conf() { return &_conf; }

private:
    static int _locked_rng(void* ctx, unsigned char* out, size_t len);

    mbedtls_ssl_config _conf;
    mbedtls_x509_crt _crt;
    mbedtls_pk_context _key;
    mbedtls_ctr_drbg_context _ctr_drbg;
    mbedtls_entropy_context _entropy;
    std::mutex _rng_lock;
    std::string _cert_pem;
};

// Server-side TLS socket. Satisfies the duck-typed interface
// r_server_threaded<SOK_T> expects (default-constructible, movable, bind/listen/
// accept, connect-to-wake) and r_socket_base, so r_web_server<r_ssl_server_socket>
// works unchanged.
//
// Two roles share this one type, as with r_socket:
//   * the listener: set_config() then bind()/listen()/accept();
//   * an accepted connection: returned by accept() with the listener's config.
//     The TLS handshake is deferred to the first send()/recv()/wait so the
//     accept thread never blocks on a slow (or hostile) client; call
//     handshake() to force it.
//
// connect() opens a plain TCP connection with no TLS. It exists so
// r_server_threaded::stop() can wake a blocked accept(); don't use it as a client.
//
// Thread-safety matches r_socket: close() may be called from another thread
// to unblock a handler parked in recv(); nothing else may be concurrent.
class r_ssl_server_socket : public r_socket_base
{
public:
    enum r_ssl_server_socket_defaults
    {
        MAX_BACKLOG = 5
    };

    R_API r_ssl_server_socket();
    R_API explicit r_ssl_server_socket(std::shared_ptr<r_ssl_server_config> cfg);
    R_API r_ssl_server_socket(r_ssl_server_socket&& obj) noexcept;
    R_API r_ssl_server_socket(const r_ssl_server_socket&) = delete;
    R_API virtual ~r_ssl_server_socket() noexcept;

    R_API r_ssl_server_socket& operator=(r_ssl_server_socket&& obj) noexcept;
    R_API r_ssl_server_socket& operator=(const r_ssl_server_socket&) = delete;

    // Required before bind(). Accepted connections inherit it.
    R_API void set_config(std::shared_ptr<r_ssl_server_config> cfg) { _cfg = std::move(cfg); }
    R_API std::shared_ptr<r_ssl_server_config> config() const { return _cfg; }

    R_API void set_io_timeout(uint64_t ioTimeOut) { _ioTimeOut = ioTimeOut; }

    // Listener side.
    R_API void bind(int port, const std::string& ip = "");
    R_API void listen(int backlog = MAX_BACKLOG);
    R_API r_ssl_server_socket accept();

    // Plain TCP connect, no TLS (see class comment).
    R_API virtual void connect(const std::string& host, int port) override;

    // Run the server handshake now. Throws on failure (the socket is then
    // invalid). No-op once done. Called implicitly by send()/recv().
    R_API void handshake();
    R_API bool handshake_done() const;

    R_API virtual void close() override;
    R_API virtual bool valid() const override;

    // Return bytes moved, or < 0 on error (mbedTLS/-1 codes, as r_ssl_socket).
    R_API virtual int send(const void* buf, size_t len) override;
    R_API virtual int recv(void* buf, size_t len) override;

    R_API virtual bool wait_till_recv_wont_block(uint64_t& millis) const override;
    R_API virtual bool wait_till_send_wont_block(uint64_t& millis) const override;

    R_API bool peer_closed() const;

    R_API std::string get_peer_ip() const;
    R_API std::string get_local_ip() const;
    R_API sock_t get_sok_id() const;

private:
    // Heap-allocated so the object is movable (mbedtls_ssl_context can't be
    // relocated once set up, and the BIO callbacks hold a pointer to the raw
    // socket).
    struct tls_state
    {
        r_raw_socket sok;
        mbedtls_ssl_context ssl;
        bool tls {false};            // false for the listener and for connect()'d wake sockets
        bool ssl_setup {false};
        bool handshaken {false};
        std::atomic<bool> failed {false};
    };

    static int _bio_send(void* ctx, const unsigned char* buf, size_t len);
    static int _bio_recv(void* ctx, unsigned char* buf, size_t len);

    // Returns false (and marks the socket failed) if the handshake can't complete.
    bool _ensure_handshake() const;
    bool _do_handshake() const;

    std::unique_ptr<tls_state> _st;
    std::shared_ptr<r_ssl_server_config> _cfg;
    uint64_t _ioTimeOut;
};

}

#endif
