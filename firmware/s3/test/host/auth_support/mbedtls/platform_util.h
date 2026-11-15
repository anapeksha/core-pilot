#pragma once

#include <cstddef>

// Host tests link only auth, not cryptography. Preserve non-elidable wiping.
inline void mbedtls_platform_zeroize(void* buffer, std::size_t size) {
  auto* bytes = static_cast<volatile unsigned char*>(buffer);
  while (size--)
    *bytes++ = 0;
}
