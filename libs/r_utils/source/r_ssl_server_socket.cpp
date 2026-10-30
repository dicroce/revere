#include "r_utils/r_ssl_server_socket.h"
#include "r_utils/r_socket_address.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_tls_fingerprint.h"

extern "C" {
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ecp.h>
#include <mbedtls/x509.h>
#include <psa/crypto.h>
}

#include <chrono>
#include <cstring>
#include <ctime>
#include <vector>

#ifndef IS_WINDOWS
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#else
#include <WinSock2.h>
#endif

using namespace r_utils;
using namespace std;

static string _mbedtls_err(int ret)
{
    char buf[128];
    mbedtls_strerror(ret, buf, sizeof(buf));
    return string(buf) + " (code: " + to_string(ret) + ")";
}

// ---------------------------------------------------------------------------
// r_ssl_server_config
// ---------------------------------------------------------------------------

int r_ssl_server_config::_locked_rng(void* ctx, unsigned char* out, size_t len)
{
    // One CTR-DRBG shared by every connection; mbedTLS is built without
    // MBEDTLS_THREADING_C so we serialize it ourselves.
    auto self = static_cast<r_ssl_server_config*>(ctx);
    lock_guard<mutex> g(self->_rng_lock);
    return mbedtls_ctr_drbg_random(&self->_ctr_drbg, out, len);
}

r_ssl_server_config::r_ssl_server_config(const string& cert_pem, const string& key_pem) :
    _cert_pem(cert_pem)
{
    mbedtls_ssl_config_init(&_conf);
    mbedtls_x509_crt_init(&_crt);
    mbedtls_pk_init(&_key);
    mbedtls_ctr_drbg_init(&_ctr_drbg);
    mbedtls_entropy_init(&_entropy);

    try
    {
        // TLS 1.3 code paths go through PSA; initialize it here, once, before
        // any worker thread can race to do it lazily inside a handshake.
        psa_crypto_init();

        const char* pers = "r_ssl_server_config";
        int ret = mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy,
                                        (const unsigned char*)pers, strlen(pers));
        if(ret != 0)
            R_THROW(("mbedtls_ctr_drbg_seed() failed: %s", _mbedtls_err(ret).c_str()));

        // PEM parsers want the terminating NUL included in the length.
        ret = mbedtls_x509_crt_parse(&_crt, (const unsigned char*)cert_pem.c_str(), cert_pem.size() + 1);
        if(ret != 0)
            R_THROW(("Unable to parse server certificate: %s", _mbedtls_err(ret).c_str()));

        ret = mbedtls_pk_parse_key(&_key, (const unsigned char*)key_pem.c_str(), key_pem.size() + 1,
                                   nullptr, 0, _locked_rng, this);
        if(ret != 0)
            R_THROW(("Unable to parse server private key: %s", _mbedtls_err(ret).c_str()));

        ret = mbedtls_pk_check_pair(&_crt.pk, &_key, _locked_rng, this);
        if(ret != 0)
            R_THROW(("Server certificate and private key don't match: %s", _mbedtls_err(ret).c_str()));

        ret = mbedtls_ssl_config_defaults(&_conf,
                                          MBEDTLS_SSL_IS_SERVER,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
        if(ret != 0)
            R_THROW(("mbedtls_ssl_config_defaults() failed: %s", _mbedtls_err(ret).c_str()));

        mbedtls_ssl_conf_rng(&_conf, _locked_rng, this);

        ret = mbedtls_ssl_conf_own_cert(&_conf, &_crt, &_key);
        if(ret != 0)
            R_THROW(("mbedtls_ssl_conf_own_cert() failed: %s", _mbedtls_err(ret).c_str()));

        // We never ask clients for a certificate; they authenticate at the
        // application layer (bearer token) and pin our fingerprint.
        mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_NONE);

        // TLS 1.2 and 1.3 (the preset default). Handshakes run concurrently on
        // the server's worker threads and go through the process-wide PSA key
        // store, which is only safe because the build defines
        // MBEDTLS_THREADING_C (see include/mbedtls/user_config.h).
    }
    catch(...)
    {
        mbedtls_ssl_config_free(&_conf);
        mbedtls_x509_crt_free(&_crt);
        mbedtls_pk_free(&_key);
        mbedtls_ctr_drbg_free(&_ctr_drbg);
        mbedtls_entropy_free(&_entropy);
        throw;
    }
}

r_ssl_server_config::~r_ssl_server_config()
{
    mbedtls_ssl_config_free(&_conf);
    mbedtls_x509_crt_free(&_crt);
    mbedtls_pk_free(&_key);
    mbedtls_ctr_drbg_free(&_ctr_drbg);
    mbedtls_entropy_free(&_entropy);
}

string r_ssl_server_config::fingerprint_sha256() const
{
    return tls_fingerprint_sha256(_crt.raw.p, _crt.raw.len);
}

static void _format_x509_time(time_t t, char out[15])
{
    struct tm tmv;
#ifdef IS_WINDOWS
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    strftime(out, 15, "%Y%m%d%H%M%S", &tmv);
}

pair<string, string> r_ssl_server_config::generate_self_signed(const string& common_name, int valid_days)
{
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);

    auto cleanup = [&]()
    {
        mbedtls_x509write_crt_free(&crt);
        mbedtls_pk_free(&key);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    };

    try
    {
        psa_crypto_init();

        const char* pers = "r_ssl_server_config::generate_self_signed";
        int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                        (const unsigned char*)pers, strlen(pers));
        if(ret != 0)
            R_THROW(("mbedtls_ctr_drbg_seed() failed: %s", _mbedtls_err(ret).c_str()));

        // ECDSA P-256: small, fast handshakes, universally supported.
        ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
        if(ret != 0)
            R_THROW(("mbedtls_pk_setup() failed: %s", _mbedtls_err(ret).c_str()));

        ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
                                  mbedtls_ctr_drbg_random, &ctr_drbg);
        if(ret != 0)
            R_THROW(("mbedtls_ecp_gen_key() failed: %s", _mbedtls_err(ret).c_str()));

        mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&crt, &key);
        mbedtls_x509write_crt_set_issuer_key(&crt, &key);

        string dn = "CN=" + common_name;
        ret = mbedtls_x509write_crt_set_subject_name(&crt, dn.c_str());
        if(ret != 0)
            R_THROW(("mbedtls_x509write_crt_set_subject_name() failed: %s", _mbedtls_err(ret).c_str()));
        ret = mbedtls_x509write_crt_set_issuer_name(&crt, dn.c_str());
        if(ret != 0)
            R_THROW(("mbedtls_x509write_crt_set_issuer_name() failed: %s", _mbedtls_err(ret).c_str()));

        // Random positive serial.
        unsigned char serial[16];
        ret = mbedtls_ctr_drbg_random(&ctr_drbg, serial, sizeof(serial));
        if(ret != 0)
            R_THROW(("mbedtls_ctr_drbg_random() failed: %s", _mbedtls_err(ret).c_str()));
        serial[0] &= 0x7f;
        serial[0] |= 0x01;
        ret = mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial));
        if(ret != 0)
            R_THROW(("mbedtls_x509write_crt_set_serial_raw() failed: %s", _mbedtls_err(ret).c_str()));

        // Valid from yesterday (clock skew between machines) for valid_days.
        time_t now = time(nullptr);
        char not_before[15], not_after[15];
        _format_x509_time(now - 24 * 3600, not_before);
        _format_x509_time(now + (time_t)valid_days * 24 * 3600, not_after);
        ret = mbedtls_x509write_crt_set_validity(&crt, not_before, not_after);
        if(ret != 0)
            R_THROW(("mbedtls_x509write_crt_set_validity() failed: %s", _mbedtls_err(ret).c_str()));

        ret = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
        if(ret != 0)
            R_THROW(("mbedtls_x509write_crt_set_basic_constraints() failed: %s", _mbedtls_err(ret).c_str()));

        ret = mbedtls_x509write_crt_set_subject_key_identifier(&crt);
        if(ret != 0)
            R_THROW(("mbedtls_x509write_crt_set_subject_key_identifier() failed: %s", _mbedtls_err(ret).c_str()));

        ret = mbedtls_x509write_crt_set_authority_key_identifier(&crt);
        if(ret != 0)
            R_THROW(("mbedtls_x509write_crt_set_authority_key_identifier() failed: %s", _mbedtls_err(ret).c_str()));

        // SANs: DNS localhost, IP 127.0.0.1, and the CN as a DNS name.
        const char* localhost = "localhost";
        unsigned char loopback[4] = {127, 0, 0, 1};

        mbedtls_x509_san_list san_cn, san_dns, san_ip;
        memset(&san_cn, 0, sizeof(san_cn));
        memset(&san_dns, 0, sizeof(san_dns));
        memset(&san_ip, 0, sizeof(san_ip));

        san_cn.node.type = MBEDTLS_X509_SAN_DNS_NAME;
        san_cn.node.san.unstructured_name.p = (unsigned char*)common_name.c_str();
        san_cn.node.san.unstructured_name.len = common_name.size();
        san_cn.next = &san_dns;

        san_dns.node.type = MBEDTLS_X509_SAN_DNS_NAME;
        san_dns.node.san.unstructured_name.p = (unsigned char*)localhost;
        san_dns.node.san.unstructured_name.len = strlen(localhost);
        san_dns.next = &san_ip;

        san_ip.node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
        san_ip.node.san.unstructured_name.p = loopback;
        san_ip.node.san.unstructured_name.len = sizeof(loopback);
        san_ip.next = nullptr;

        ret = mbedtls_x509write_crt_set_subject_alternative_name(&crt, &san_cn);
        if(ret != 0)
            R_THROW(("mbedtls_x509write_crt_set_subject_alternative_name() failed: %s", _mbedtls_err(ret).c_str()));

        vector<unsigned char> buf(4096);
        ret = mbedtls_x509write_crt_pem(&crt, buf.data(), buf.size(), mbedtls_ctr_drbg_random, &ctr_drbg);
        if(ret != 0)
            R_THROW(("mbedtls_x509write_crt_pem() failed: %s", _mbedtls_err(ret).c_str()));
        string cert_pem((const char*)buf.data());

        ret = mbedtls_pk_write_key_pem(&key, buf.data(), buf.size());
        if(ret != 0)
            R_THROW(("mbedtls_pk_write_key_pem() failed: %s", _mbedtls_err(ret).c_str()));
        string key_pem((const char*)buf.data());

        // Don't leave the private key lying around in a reusable buffer.
        memset(buf.data(), 0, buf.size());

        cleanup();
        return {cert_pem, key_pem};
    }
    catch(...)
    {
        cleanup();
        throw;
    }
}

// ---------------------------------------------------------------------------
// r_ssl_server_socket
// ---------------------------------------------------------------------------

void r_ssl_server_socket::set_io_timeout(uint64_t ioTimeOut)
{
    _ioTimeOut = ioTimeOut;
    // An accepted connection's socket already has accept()'s timeouts, which
    // bound each blocking recv()/send() inside the handshake: change them too.
    if(_st && _st->tls && _st->sok.valid())
    {
        sock_t s = (sock_t)_st->sok.get_sok_id();
#ifdef IS_WINDOWS
        // Winsock takes a DWORD of milliseconds here (0 would mean no timeout).
        DWORD io_timeout = (DWORD)(ioTimeOut > 0 ? ioTimeOut : 1);
#else
        timeval io_timeout;
        io_timeout.tv_sec = (long)(ioTimeOut / 1000);
        io_timeout.tv_usec = (long)((ioTimeOut % 1000) * 1000);
        if(io_timeout.tv_sec == 0 && io_timeout.tv_usec == 0)
            io_timeout.tv_usec = 1000; // a zero timeval means no timeout
#endif
        ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&io_timeout, sizeof(io_timeout));
        ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&io_timeout, sizeof(io_timeout));
    }
}

int r_ssl_server_socket::_bio_send(void* ctx, const unsigned char* buf, size_t len)
{
    int ret = static_cast<r_raw_socket*>(ctx)->send(buf, len);
    if(ret < 0)
    {
#ifdef IS_WINDOWS
        int err = WSAGetLastError();
        if(err == WSAEWOULDBLOCK || err == WSAETIMEDOUT)
            return MBEDTLS_ERR_SSL_WANT_WRITE;
#else
        if(errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)
            return MBEDTLS_ERR_SSL_WANT_WRITE;
#endif
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return ret;
}

int r_ssl_server_socket::_bio_recv(void* ctx, unsigned char* buf, size_t len)
{
    int ret = static_cast<r_raw_socket*>(ctx)->recv(buf, len);
    if(ret < 0)
    {
#ifdef IS_WINDOWS
        int err = WSAGetLastError();
        if(err == WSAEWOULDBLOCK || err == WSAETIMEDOUT)
            return MBEDTLS_ERR_SSL_WANT_READ;
#else
        if(errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)
            return MBEDTLS_ERR_SSL_WANT_READ;
#endif
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return ret;
}

r_ssl_server_socket::r_ssl_server_socket() :
    _st(new tls_state()),
    _cfg(),
    _ioTimeOut(10000)
{
}

r_ssl_server_socket::r_ssl_server_socket(shared_ptr<r_ssl_server_config> cfg) :
    _st(new tls_state()),
    _cfg(move(cfg)),
    _ioTimeOut(10000)
{
}

r_ssl_server_socket::r_ssl_server_socket(r_ssl_server_socket&& obj) noexcept :
    _st(move(obj._st)),
    _cfg(move(obj._cfg)),
    _ioTimeOut(obj._ioTimeOut)
{
}

r_ssl_server_socket::~r_ssl_server_socket() noexcept
{
    if(!_st)
        return;

    if(_st->tls && _st->handshaken && !_st->failed && _st->sok.valid())
    {
        // Best effort; a peer that's gone just makes this fail fast.
        mbedtls_ssl_close_notify(&_st->ssl);
    }

    _st->sok.close();

    if(_st->ssl_setup)
        mbedtls_ssl_free(&_st->ssl);
}

r_ssl_server_socket& r_ssl_server_socket::operator=(r_ssl_server_socket&& obj) noexcept
{
    if(this != &obj)
    {
        if(_st)
        {
            _st->sok.close();
            if(_st->ssl_setup)
                mbedtls_ssl_free(&_st->ssl);
        }
        _st = move(obj._st);
        _cfg = move(obj._cfg);
        _ioTimeOut = obj._ioTimeOut;
    }
    return *this;
}

void r_ssl_server_socket::bind(int port, const string& ip)
{
    if(!_cfg)
        R_THROW(("r_ssl_server_socket::bind(): no r_ssl_server_config set; call set_config() first."));
    if(!_st)
        R_THROW(("r_ssl_server_socket::bind() on a moved-from socket."));

    _st->sok.bind(port, ip);
}

void r_ssl_server_socket::listen(int backlog)
{
    if(!_st)
        R_THROW(("r_ssl_server_socket::listen() on a moved-from socket."));
    _st->sok.listen(backlog);
}

r_ssl_server_socket r_ssl_server_socket::accept()
{
    if(!_st)
        R_THROW(("r_ssl_server_socket::accept() on a moved-from socket."));
    if(!_cfg)
        R_THROW(("r_ssl_server_socket::accept(): no r_ssl_server_config set."));

    r_ssl_server_socket conn(_cfg);
    conn._ioTimeOut = _ioTimeOut;
    conn._st->sok = _st->sok.accept();
    conn._st->tls = true;

    // Bound the time a blocking recv()/send() inside mbedTLS can take, as the
    // client does; the WANT_READ/WANT_WRITE retry loops do the real waiting.
    timeval io_timeout;
    io_timeout.tv_sec = (long)(_ioTimeOut / 1000);
    io_timeout.tv_usec = (long)((_ioTimeOut % 1000) * 1000);
    sock_t s = (sock_t)conn._st->sok.get_sok_id();
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&io_timeout, sizeof(io_timeout));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&io_timeout, sizeof(io_timeout));

    mbedtls_ssl_init(&conn._st->ssl);
    conn._st->ssl_setup = true;
    int ret = mbedtls_ssl_setup(&conn._st->ssl, _cfg->conf());
    if(ret != 0)
        R_THROW(("mbedtls_ssl_setup() failed: %s", _mbedtls_err(ret).c_str()));

    mbedtls_ssl_set_bio(&conn._st->ssl, &conn._st->sok, _bio_send, _bio_recv, nullptr);

    return conn;
}

void r_ssl_server_socket::connect(const string& host, int port)
{
    if(!_st)
        R_THROW(("r_ssl_server_socket::connect() on a moved-from socket."));

    // Plain TCP, no TLS: this exists to wake a blocked accept().
    _st->sok.create(r_socket_address::get_address_family(host));
    _st->sok.connect(host, port, _ioTimeOut);
    _st->tls = false;
}

bool r_ssl_server_socket::handshake_done() const
{
    return _st && _st->tls && _st->handshaken;
}

void r_ssl_server_socket::handshake()
{
    if(!_st || !_st->tls)
        R_THROW(("r_ssl_server_socket::handshake(): not an accepted TLS connection."));
    if(!_ensure_handshake())
        R_THROW(("TLS server handshake failed."));
}

bool r_ssl_server_socket::_ensure_handshake() const
{
    if(!_st || !_st->tls)
        return false;
    if(_st->handshaken)
        return !_st->failed;
    if(_st->failed)
        return false;
    return _do_handshake();
}

bool r_ssl_server_socket::_do_handshake() const
{
    // const because wait_till_recv_wont_block() is const and must be able to
    // complete the handshake; the state lives behind the unique_ptr.
    auto deadline = chrono::steady_clock::now() + chrono::milliseconds(_ioTimeOut);

    int ret;
    while((ret = mbedtls_ssl_handshake(&_st->ssl)) != 0)
    {
        if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            R_LOG_INFO("TLS handshake from %s failed: %s",
                       _st->sok.valid() ? _st->sok.get_peer_ip().c_str() : "?", _mbedtls_err(ret).c_str());
            _st->failed = true;
            _st->sok.close();
            return false;
        }

        auto left = chrono::duration_cast<chrono::milliseconds>(deadline - chrono::steady_clock::now()).count();
        if(left <= 0)
        {
            R_LOG_INFO("TLS handshake from %s timed out.",
                       _st->sok.valid() ? _st->sok.get_peer_ip().c_str() : "?");
            _st->failed = true;
            _st->sok.close();
            return false;
        }

        uint64_t wait_ms = (uint64_t)left;
        bool ready = (ret == MBEDTLS_ERR_SSL_WANT_READ)
            ? _st->sok.wait_till_recv_wont_block(wait_ms)
            : _st->sok.wait_till_send_wont_block(wait_ms);
        if(!ready && !_st->sok.valid())
        {
            _st->failed = true;
            return false;
        }
    }

    _st->handshaken = true;
    return true;
}

void r_ssl_server_socket::close()
{
    // Only the raw socket: this may be called from another thread to unblock a
    // handler parked in recv(), and the mbedTLS context isn't safe to touch
    // concurrently. The peer sees a FIN without close_notify, which HTTP
    // clients treat as end of stream.
    if(_st)
        _st->sok.close();
}

bool r_ssl_server_socket::valid() const
{
    return _st && !_st->failed && _st->sok.valid();
}

int r_ssl_server_socket::send(const void* buf, size_t len)
{
    if(!valid())
        return MBEDTLS_ERR_SSL_CONN_EOF;

    if(!_st->tls)
        return _st->sok.send(buf, len);

    if(!_ensure_handshake())
        return MBEDTLS_ERR_SSL_CONN_EOF;

    const int MAX_RETRIES = 50;
    for(int retry = 0; retry < MAX_RETRIES; ++retry)
    {
        int ret = mbedtls_ssl_write(&_st->ssl, static_cast<const unsigned char*>(buf), len);
        if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            return ret;

        uint64_t timeout_ms = 100;
        if(!_st->sok.wait_till_send_wont_block(timeout_ms))
            return MBEDTLS_ERR_SSL_WANT_WRITE;
    }

    return MBEDTLS_ERR_SSL_WANT_WRITE;
}

int r_ssl_server_socket::recv(void* buf, size_t len)
{
    if(!valid())
        return MBEDTLS_ERR_SSL_CONN_EOF;

    if(!_st->tls)
        return _st->sok.recv(buf, len);

    if(!_ensure_handshake())
        return MBEDTLS_ERR_SSL_CONN_EOF;

    const int MAX_RETRIES = 50;
    for(int retry = 0; retry < MAX_RETRIES; ++retry)
    {
        int ret = mbedtls_ssl_read(&_st->ssl, static_cast<unsigned char*>(buf), len);
        if(ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
            return 0;
        if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            return ret;

        uint64_t timeout_ms = 100;
        if(!_st->sok.wait_till_recv_wont_block(timeout_ms))
            return MBEDTLS_ERR_SSL_WANT_READ;
    }

    return MBEDTLS_ERR_SSL_WANT_READ;
}

bool r_ssl_server_socket::wait_till_recv_wont_block(uint64_t& millis) const
{
    if(!valid())
        return false;

    if(!_st->tls)
        return _st->sok.wait_till_recv_wont_block(millis);

    if(!_st->handshaken)
    {
        // The first thing to arrive is the ClientHello; completing the
        // handshake here means a following recv() returns application data.
        if(!_st->sok.wait_till_recv_wont_block(millis))
            return false;
        if(!_ensure_handshake())
            return false;
    }

    if(mbedtls_ssl_get_bytes_avail(&_st->ssl) > 0)
        return true;

    return _st->sok.wait_till_recv_wont_block(millis);
}

bool r_ssl_server_socket::wait_till_send_wont_block(uint64_t& millis) const
{
    if(!valid())
        return false;
    return _st->sok.wait_till_send_wont_block(millis);
}

bool r_ssl_server_socket::peer_closed() const
{
    if(!_st)
        return true;
    if(_st->tls && _st->ssl_setup && mbedtls_ssl_get_bytes_avail(&_st->ssl) > 0)
        return false;
    return _st->sok.peer_closed();
}

string r_ssl_server_socket::get_peer_ip() const
{
    if(!_st)
        R_THROW(("r_ssl_server_socket::get_peer_ip() on a moved-from socket."));
    return _st->sok.get_peer_ip();
}

string r_ssl_server_socket::get_local_ip() const
{
    if(!_st)
        R_THROW(("r_ssl_server_socket::get_local_ip() on a moved-from socket."));
    return _st->sok.get_local_ip();
}

sock_t r_ssl_server_socket::get_sok_id() const
{
    if(!_st)
        return kInvalidSock;
    return _st->sok.get_sok_id();
}
