#include "core_security.hpp"
#include <psa/crypto.h>

// Host entropy only; the production ESP implementation remains target-only.
core_security::Status core_security::EspHardwareRandom::fill(uint8_t* out, size_t size) {
  if (!out && size)
    return Status::Invalid;
  if (psa_crypto_init() != PSA_SUCCESS)
    return Status::CryptoError;
  return psa_generate_random(out, size) == PSA_SUCCESS ? Status::Ok : Status::CryptoError;
}
