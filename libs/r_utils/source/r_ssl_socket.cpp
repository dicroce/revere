#include "r_utils/r_ssl_socket.h"
#include "r_utils/r_socket_address.h"
#include "r_utils/r_exception.h"
#include <stdexcept>
#include <cstring>

#ifndef IS_WINDOWS
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#else
#include <WinSock2.h>
#endif

#ifdef IS_WINDOWS
#include <windows.h>
#include <wincrypt.h>
#else
#include <fstream>
#endif

using namespace r_utils;

static int _mbedtls_send(void* ctx, const unsigned char* buf, size_t len)
{
    int ret = static_cast<r_raw_socket*>(ctx)->send(buf, len);
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

static int _mbedtls_recv(void* ctx, unsigned char* buf, size_t len)
{
    int ret = static_cast<r_raw_socket*>(ctx)->recv(buf, len);
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
        mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
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

void r_ssl_socket::connect(const std::string& host, int port)
{
    std::lock_guard<std::recursive_mutex> lock(_sslLock);

    _host = host;

    // Create socket with correct address family (IPv4 or IPv6)
    _sok.create(r_socket_address::get_address_family(host));

    // Set socket timeouts before connecting
    timeval connect_timeout;
    connect_timeout.tv_sec = (long)(_ioTimeOut / 1000);
    connect_timeout.tv_usec = (long)((_ioTimeOut % 1000) * 1000);

    if( ::setsockopt( (SOK)_sok.get_sok_id(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&connect_timeout, sizeof(connect_timeout) ) < 0 )
        R_THROW(("Unable to configure socket receive timeout."));

    if( ::setsockopt( (SOK)_sok.get_sok_id(), SOL_SOCKET, SO_SNDTIMEO, (const char*)&connect_timeout, sizeof(connect_timeout) ) < 0 )
        R_THROW(("Unable to configure socket send timeout."));

    _sok.connect(host, port);

    mbedtls_ssl_set_bio(&_ssl, &_sok, _mbedtls_send, _mbedtls_recv, nullptr);

    if (mbedtls_ssl_set_hostname(&_ssl, host.c_str()) != 0)
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

    _valid = true;
}

void r_ssl_socket::close() const
{
    std::lock_guard<std::recursive_mutex> lock(_sslLock);
    if (_valid) {
        mbedtls_ssl_close_notify(&_ssl);
        _sok.close();
        _valid = false;
    }
}

bool r_ssl_socket::valid() const
{
    std::lock_guard<std::recursive_mutex> lock(_sslLock);
    return _valid && _sok.valid();
}

int r_ssl_socket::send(const void* buf, size_t len)
{
    std::lock_guard<std::recursive_mutex> lock(_sslLock);

    if (!_valid)
        return MBEDTLS_ERR_SSL_CONN_EOF;

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
    std::lock_guard<std::recursive_mutex> lock(_sslLock);

    if (!_valid)
        return MBEDTLS_ERR_SSL_CONN_EOF;

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
    std::lock_guard<std::recursive_mutex> lock(_sslLock);

    if (!_valid)
        return false;

    // Check if mbedtls has already buffered decrypted data
    if (mbedtls_ssl_get_bytes_avail(&_ssl) > 0)
        return true;

    // Otherwise wait for underlying socket to have data
    return _sok.wait_till_recv_wont_block(millis);
}

bool r_ssl_socket::wait_till_send_wont_block(uint64_t& millis) const
{
    std::lock_guard<std::recursive_mutex> lock(_sslLock);

    if (!_valid)
        return false;

    return _sok.wait_till_send_wont_block(millis);
}
