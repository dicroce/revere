// include/mbedtls/config.h

#ifndef MBEDTLS_USER_CONFIG_FILE
#define MBEDTLS_USER_CONFIG_FILE "mbedtls/user_config.h"
#endif

#include "mbedtls/config_adjust_legacy_crypto.h"
#include "mbedtls/config_adjust_psa_from_legacy.h"
#include "mbedtls/config_adjust_psa_superset_legacy.h"
#include "mbedtls/config_adjust_ssl.h"
#include "mbedtls/config_adjust_x509.h"
#include "mbedtls/config_psa.h"
