#include "r_utils/r_ssl_socket.h"
#include "r_utils/r_socket_address.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_tls_fingerprint.h"
#include <stdexcept>
#include <cstring>

#ifndef IS_WINDOWS
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#else
#include <WinSock2.h>
#include <WS2tcpip.h>
#endif

#ifdef IS_WINDOWS
#include <windows.h>
#include <wincrypt.h>
#include <vector>
#else
#include <fstream>
#endif

using namespace r_utils;

bool r_ssl_socket::accepts_verify_result(uint32_t flags, const std::function<bool()>& os_trusts)
{
    if (flags == 0)
        return true;
    // Exactly "not trusted": no other flag with it (expired, a name that doesn't match, a revoked or weak certificate,
    // never verified at all).
    if (flags != MBEDTLS_X509_BADCERT_NOT_TRUSTED)
        return false;
    return os_trusts && os_trusts();
}

#ifdef IS_WINDOWS
// Whether Windows trusts the chain the server presented, for this host: its own chain building (which fetches a root
// it doesn't have yet, as Automatic Root Certificates Update does; enumerating the ROOT store never does) and then the
// SSL policy for a server, with the name checked. No revocation checking, as before. An address in place of a name gets
// no fallback: the certificate has to be one this process already trusts. The wait on the network is bounded.
static bool _windows_trusts(const mbedtls_ssl_context* ssl, const std::string& host, uint64_t timeout_ms)
{
    const mbedtls_x509_crt* leaf = mbedtls_ssl_get_peer_cert(ssl);
    if (!leaf || !leaf->raw.p || leaf->raw.len == 0)
        return false;

    in_addr a4;
    in6_addr a6;
    if (host.empty() || inet_pton(AF_INET, host.c_str(), &a4) == 1 || inet_pton(AF_INET6, host.c_str(), &a6) == 1)
        return false;

    // The name as Windows wants it: UTF-16, converted properly (a name that isn't valid is no name).
    const int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host.c_str(), -1, nullptr, 0);
    if (wide_len <= 1)
        return false;
    std::vector<wchar_t> wide_host((size_t)wide_len);
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host.c_str(), -1, wide_host.data(), wide_len) == 0)
        return false;

    PCCERT_CONTEXT leaf_ctx = CertCreateCertificateContext(X509_ASN_ENCODING, leaf->raw.p, (DWORD)leaf->raw.len);
    if (!leaf_ctx)
        return false;

    bool trusted = false;
    HCERTSTORE extra = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    if (extra)
    {
        // The certificates the server sent after its own: the intermediates Windows may need.
        for (const mbedtls_x509_crt* c = leaf->next; c; c = c->next)
            CertAddEncodedCertificateToStore(extra, X509_ASN_ENCODING, c->raw.p, (DWORD)c->raw.len, CERT_STORE_ADD_USE_EXISTING, nullptr);

        static char server_auth[] = szOID_PKIX_KP_SERVER_AUTH;
        LPSTR usages[] = { server_auth };
        CERT_CHAIN_PARA para;
        memset(&para, 0, sizeof(para));
        para.cbSize = sizeof(para);
        para.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
        para.RequestedUsage.Usage.cUsageIdentifier = 1;
        para.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
        para.dwUrlRetrievalTimeout = (DWORD)(timeout_ms > 0xFFFFFF ? 0xFFFFFF : timeout_ms);

        if (CertGetCertificateChain(nullptr, leaf_ctx, nullptr, extra, &para, 0, nullptr, &chain) && chain)
        {
            SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl_para;
            memset(&ssl_para, 0, sizeof(ssl_para));
            ssl_para.cbSize = sizeof(ssl_para);
            ssl_para.dwAuthType = AUTHTYPE_SERVER;
            ssl_para.pwszServerName = wide_host.data();

            CERT_CHAIN_POLICY_PARA policy;
            memset(&policy, 0, sizeof(policy));
            policy.cbSize = sizeof(policy);
            policy.pvExtraPolicyPara = &ssl_para;

            CERT_CHAIN_POLICY_STATUS status;
            memset(&status, 0, sizeof(status));
            status.cbSize = sizeof(status);

            if (CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status))
                trusted = (status.dwError == 0);
        }
    }

    if (chain)
        CertFreeCertificateChain(chain);
    if (extra)
        CertCloseStore(extra, 0);
    CertFreeCertificateContext(leaf_ctx);
    return trusted;
}
#endif

int r_ssl_socket::_bio_send(void* ctx, const unsigned char* buf, size_t len)
{
    int ret = static_cast<r_ssl_socket*>(ctx)->_sok.send(buf, len);
    if (ret < 0)
    {
#ifdef IS_WINDOWS
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT)
            return MBEDTLS_ERR_SSL_WANT_WRITE;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)
            return MBEDTLS_ERR_SSL_WANT_WRITE;
#endif
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return ret;
}

int r_ssl_socket::_bio_recv(void* ctx, unsigned char* buf, size_t len)
{
    auto* self = static_cast<r_ssl_socket*>(ctx);
#ifdef IS_WINDOWS
    // Every read of the connection waits first, with the socket's own wait, which a shutdown() from another thread
    // ends (a blocking recv() isn't woken by it on Windows: it would wait out the whole timeout). It has to be here,
    // under mbedtls_ssl_read(), and not before it: one read goes on to further records through this callback (the
    // TLS 1.3 session tickets that follow the handshake, a HelloRequest, an empty record, a record still arriving).
    // After a shutdown the wait answers at once and the recv() below sees the end.
    uint64_t wait_ms = self->_ioTimeOut;
    if (!self->_sok.wait_till_recv_wont_block(wait_ms))
        return self->_sok.valid() ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
#endif
    int ret = self->_sok.recv(buf, len);
    if (ret < 0)
    {
#ifdef IS_WINDOWS
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT)
            return MBEDTLS_ERR_SSL_WANT_READ;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)
            return MBEDTLS_ERR_SSL_WANT_READ;
#endif
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return ret;
}

r_ssl_socket::r_ssl_socket(bool enable_auth) :
    _valid(false), _ioTimeOut(10000)
{
    mbedtls_ssl_init(&_ssl);
    mbedtls_ssl_config_init(&_conf);
    mbedtls_ctr_drbg_init(&_ctr_drbg);
    mbedtls_entropy_init(&_entropy);
    mbedtls_x509_crt_init(&_ca_cert);

    const char* pers = "r_ssl_socket";
    if (mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy,
                              (const unsigned char*)pers, strlen(pers)) != 0)
        throw std::runtime_error("mbedtls_ctr_drbg_seed() failed");

    if (mbedtls_ssl_config_defaults(&_conf,
                                    MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        throw std::runtime_error("mbedtls_ssl_config_defaults() failed");

    mbedtls_ssl_conf_rng(&_conf, mbedtls_ctr_drbg_random, &_ctr_drbg);

    if (enable_auth)
    {
        bool loaded = false;

#ifdef IS_WINDOWS
        HCERTSTORE hStore = CertOpenSystemStore(0, "ROOT");
        if (hStore)
        {
            PCCERT_CONTEXT pContext = nullptr;
            while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != nullptr)
            {
                int ret = mbedtls_x509_crt_parse_der(&_ca_cert,
                    pContext->pbCertEncoded, pContext->cbCertEncoded);
                if (ret == 0)
                    loaded = true;
            }
            CertCloseStore(hStore, 0);
        }
#endif
#ifdef IS_LINUX
        const char* candidates[] = {
            "/etc/ssl/certs/ca-certificates.crt",
            "/etc/pki/tls/certs/ca-bundle.crt",
            "/etc/ssl/cert.pem",
            "/etc/ssl/certs/ca-bundle.crt"
        };

        for (const auto& path : candidates)
        {
            std::ifstream f(path);
            if (!f)
                continue;

            int rc = mbedtls_x509_crt_parse_file(&_ca_cert, path);
            if (rc == 0)
            {
                loaded = true;
                break;
            }
        }
#endif
#ifdef IS_MACOS
        const char* candidates[] = {
            "/etc/ssl/cert.pem",
            "/usr/local/etc/openssl/cert.pem",
            "/usr/local/etc/openssl@1.1/cert.pem",
            "/System/Library/OpenSSL/certs/cert.pem"
        };

        for (const auto& path : candidates)
        {
            std::ifstream f(path);
            if (!f)
                continue;

            int rc = mbedtls_x509_crt_parse_file(&_ca_cert, path);
            if (rc == 0)
            {
                loaded = true;
                break;
            }
        }
#endif

        if (!loaded)
            throw std::runtime_error("Failed to load system root certificates");

        mbedtls_ssl_conf_ca_chain(&_conf, &_ca_cert, nullptr);
#if defined(IS_WINDOWS) || defined(R_FORCE_TLS_VERIFY_AFTER_HANDSHAKE)
        // Optional: the handshake goes on past an untrusted root, and start_tls reads the verdict before anything is
        // sent (accepts_verify_result), so Windows can be asked about a root it hasn't fetched yet. (The second macro
        // is for a test build on another platform, where the operating system is never asked and nothing is trusted
        // beyond what mbedtls already does.)
        mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
        _verify_after_handshake = true;
#else
        mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
#endif
    }
    else
    {
        mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_NONE);
    }

    if (mbedtls_ssl_setup(&_ssl, &_conf) != 0)
        throw std::runtime_error("mbedtls_ssl_setup() failed");
}

r_ssl_socket::~r_ssl_socket()
{
    close();
    mbedtls_x509_crt_free(&_ca_cert);
    mbedtls_ssl_free(&_ssl);
    mbedtls_ssl_config_free(&_conf);
    mbedtls_ctr_drbg_free(&_ctr_drbg);
    mbedtls_entropy_free(&_entropy);
}

void r_ssl_socket::connect_plain(const std::string& host, int port)
{
    _host = host;

    // Create socket with correct address family (IPv4 or IPv6)
    _sok.create(r_socket_address::get_address_family(host));

    // Set socket timeouts before connecting
    timeval connect_timeout;
    connect_timeout.tv_sec = (long)(_ioTimeOut / 1000);
    connect_timeout.tv_usec = (long)((_ioTimeOut % 1000) * 1000);

    if( ::setsockopt( (sock_t)_sok.get_sok_id(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&connect_timeout, sizeof(connect_timeout) ) < 0 )
        R_THROW(("Unable to configure socket receive timeout."));

    if( ::setsockopt( (sock_t)_sok.get_sok_id(), SOL_SOCKET, SO_SNDTIMEO, (const char*)&connect_timeout, sizeof(connect_timeout) ) < 0 )
        R_THROW(("Unable to configure socket send timeout."));

    // The timeouts above don't cover connect() itself on Windows.
    _sok.connect(host, port, _ioTimeOut);

    _plain = true;
    _valid = true;
}

void r_ssl_socket::connect(const std::string& host, int port)
{
    connect_plain(host, port);
    start_tls();
}

void r_ssl_socket::start_tls()
{
    if (!_plain)
        R_THROW(("r_ssl_socket::start_tls(): not connected, or TLS already started."));
    _valid = false;
    _plain = false;

    mbedtls_ssl_set_bio(&_ssl, this, _bio_send, _bio_recv, nullptr);

    if (mbedtls_ssl_set_hostname(&_ssl, _host.c_str()) != 0)
        R_THROW(("mbedtls_ssl_set_hostname() failed"));

    int ret;
    while ((ret = mbedtls_ssl_handshake(&_ssl)) != 0)
    {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            char error_buf[100];
            mbedtls_strerror(ret, error_buf, sizeof(error_buf));
            std::string error_msg = std::string("mbedtls_ssl_handshake() failed: ") + error_buf + " (code: " + std::to_string(ret) + ")";
            R_THROW((error_msg.c_str()));
        }
    }

    if (_verify_after_handshake)
    {
        // Before anything is sent or read on the connection: the handshake has only established who holds the key.
        const uint32_t flags = mbedtls_ssl_get_verify_result(&_ssl);
        bool os_trusts_it_called = false;
        const bool accepted = accepts_verify_result(flags, [&]() {
            os_trusts_it_called = true;
#ifdef IS_WINDOWS
            return _windows_trusts(&_ssl, _host, _ioTimeOut);
#else
            return false;
#endif
        });
        if (!accepted)
        {
            char info[512];
            mbedtls_x509_crt_verify_info(info, sizeof(info), "", flags);
            _sok.close();
            std::string detail(info);
            while (!detail.empty() && (detail.back() == '\n' || detail.back() == ' '))
                detail.pop_back();
            R_THROW(("Certificate verification failed%s: %s", os_trusts_it_called ? " (Windows does not trust it either)" : "", detail.c_str()));
        }
    }

    _valid = true;
}

void r_ssl_socket::close()
{
    if (_valid && _plain) {
        _sok.close();
        _valid = false;
        _plain = false;
    }
    if (_valid) {
        mbedtls_ssl_close_notify(&_ssl);
        _sok.close();
        _valid = false;
    }
}

bool r_ssl_socket::valid() const
{
    return _valid && _sok.valid();
}

int r_ssl_socket::send(const void* buf, size_t len)
{
    if (!_valid)
        return MBEDTLS_ERR_SSL_CONN_EOF;
    if (_plain)
        return _sok.send(buf, len);

    // Retry loop for WANT_READ/WANT_WRITE with a short timeout per attempt
    // This handles the case where SSL needs multiple network writes to encrypt and send data
    uint64_t timeout_ms = 100;  // Short timeout per write attempt
    const int MAX_RETRIES = 50;  // Allow up to 5 seconds total (50 * 100ms)

    for (int retry = 0; retry < MAX_RETRIES; ++retry)
    {
        int ret = mbedtls_ssl_write(&_ssl, static_cast<const unsigned char*>(buf), len);

        // If we sent data or got a real error (not WANT_READ/WRITE), return it
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            return ret;

        // WANT_READ/WANT_WRITE means we need to wait for the socket
        // Wait for the underlying socket to be ready, then retry
        timeout_ms = 100;  // Reset timeout for each wait
        if (!_sok.wait_till_send_wont_block(timeout_ms))
            return MBEDTLS_ERR_SSL_WANT_WRITE;  // Timeout waiting for socket
    }

    // Exceeded max retries
    return MBEDTLS_ERR_SSL_WANT_WRITE;
}

int r_ssl_socket::recv(void* buf, size_t len)
{
    if (!_valid)
        return MBEDTLS_ERR_SSL_CONN_EOF;
    if (_plain)
        return _sok.recv(buf, len);

    // Retry loop for WANT_READ/WANT_WRITE with a short timeout per attempt
    // This handles the case where SSL needs multiple network reads to decrypt a record
    uint64_t timeout_ms = 100;  // Short timeout per read attempt
    const int MAX_RETRIES = 50;  // Allow up to 5 seconds total (50 * 100ms)

    for (int retry = 0; retry < MAX_RETRIES; ++retry)
    {
        int ret = mbedtls_ssl_read(&_ssl, static_cast<unsigned char*>(buf), len);

        // If we got data or a real error (not WANT_READ/WRITE), return it
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            return ret;

        // WANT_READ/WANT_WRITE means we need more network data
        // Wait for the underlying socket to have data, then retry
        timeout_ms = 100;  // Reset timeout for each wait
        if (!_sok.wait_till_recv_wont_block(timeout_ms))
            return MBEDTLS_ERR_SSL_WANT_READ;  // Timeout waiting for data
    }

    // Exceeded max retries
    return MBEDTLS_ERR_SSL_WANT_READ;
}

bool r_ssl_socket::wait_till_recv_wont_block(uint64_t& millis) const
{
    if (!_valid)
        return false;
    if (_plain)
        return _sok.wait_till_recv_wont_block(millis);

    // Check if mbedtls has already buffered decrypted data
    if (mbedtls_ssl_get_bytes_avail(&_ssl) > 0)
        return true;

    // Otherwise wait for underlying socket to have data
    return _sok.wait_till_recv_wont_block(millis);
}

std::string r_ssl_socket::peer_cert_fingerprint_sha256() const
{
    if (!_valid)
        return std::string();
    const mbedtls_x509_crt* crt = mbedtls_ssl_get_peer_cert(&_ssl);
    if (!crt || !crt->raw.p || crt->raw.len == 0)
        return std::string();
    return tls_fingerprint_sha256(crt->raw.p, crt->raw.len);
}

bool r_ssl_socket::peer_closed() const
{
    if (!_valid)
        return true;

    // Decrypted-but-unread data counts as "still talking".
    if (mbedtls_ssl_get_bytes_avail(&_ssl) > 0)
        return false;

    return _sok.peer_closed();
}

bool r_ssl_socket::wait_till_send_wont_block(uint64_t& millis) const 
{
    if (!_valid)
        return false;

    return _sok.wait_till_send_wont_block(millis);
}
