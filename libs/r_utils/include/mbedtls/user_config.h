// include/mbedtls/user_config.h

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_PROTO_TLS1_3
#define MBEDTLS_NET_C
#define MBEDTLS_ERROR_C
#define MBEDTLS_DEBUG_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_PK_C
#define MBEDTLS_RSA_C
#define MBEDTLS_BIGNUM_C

// DTLS-SRTP (RFC 5764): how WebRTC keys its media. The cloud plugin publishes
// live view over WebRTC (WHIP) with this Mbed TLS. It changes the layout of
// mbedtls_ssl_config/mbedtls_ssl_context, so it lives here, where everything
// built against these headers sees it.
#define MBEDTLS_SSL_DTLS_SRTP
