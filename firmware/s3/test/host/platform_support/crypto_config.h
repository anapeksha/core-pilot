#pragma once
// Only the production record codec's algorithms; no asymmetric/TLS dependencies.
#define TF_PSA_CRYPTO_CONFIG_VERSION 0x01000000
#define MBEDTLS_PSA_CRYPTO_C
#define MBEDTLS_PSA_BUILTIN_GET_ENTROPY
#define MBEDTLS_MD_C
#define MBEDTLS_HMAC_DRBG_C
#define PSA_WANT_ALG_SHA_256 1
#define PSA_WANT_ALG_HMAC 1
#define PSA_WANT_ALG_HKDF 1
#define PSA_WANT_KEY_TYPE_HMAC 1
#define PSA_WANT_KEY_TYPE_DERIVE 1
#define PSA_WANT_ALG_CHACHA20_POLY1305 1
#define PSA_WANT_KEY_TYPE_CHACHA20 1
