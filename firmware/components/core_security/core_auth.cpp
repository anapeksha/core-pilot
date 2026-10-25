#include "core_auth.hpp"

#include "mbedtls/platform_util.h"

#include <cstring>

namespace core_security {
namespace {
bool equal(const char* a, const char* b, size_t size) {
  if (!a || std::strlen(a) != size)
    return false;
  unsigned diff = 0;
  for (size_t i = 0; i < size; ++i)
    diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  return diff == 0;
}
Status randomHex(RandomSource& random, char* out, size_t bytes) {
  uint8_t data[32]{};
  const auto result = random.fill(data, bytes);
  if (result == Status::Ok) {
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < bytes; ++i) {
      out[2 * i] = hex[data[i] >> 4];
      out[2 * i + 1] = hex[data[i] & 15];
    }
    out[bytes * 2] = 0;
  }
  mbedtls_platform_zeroize(data, sizeof(data));
  return result;
}
} // namespace
Auth::~Auth() {
  mbedtls_platform_zeroize(code_, sizeof(code_));
  mbedtls_platform_zeroize(sessions_, sizeof(sessions_));
}
Status Auth::begin(RandomSource& random, uint64_t now, BootstrapSink sink) {
  mbedtls_platform_zeroize(code_, sizeof(code_));
  mbedtls_platform_zeroize(sessions_, sizeof(sessions_));
  attempts_ = 0;
  bootstrap_expires_ = 0;
  const auto result = randomHex(random, code_, 8);
  if (result == Status::Ok) {
    bootstrap_expires_ = now + kBootstrapSeconds;
    if (sink)
      sink(code_, bootstrap_expires_);
  }
  return result;
}
bool Auth::bootstrap_open(uint64_t now) {
  if (now >= bootstrap_expires_ || attempts_ >= 5) {
    mbedtls_platform_zeroize(code_, sizeof(code_));
    return false;
  }
  return true;
}
bool Auth::authorize(const char* token, uint64_t now) {
  bool valid = false;
  for (auto& session : sessions_) {
    if (now >= session.expires)
      mbedtls_platform_zeroize(&session, sizeof(session));
    if (session.expires && equal(token, session.token, 64))
      valid = true;
  }
  return valid;
}
Status Auth::exchange(const char* code, RandomSource& random, uint64_t now, char (&token)[65]) {
  mbedtls_platform_zeroize(token, sizeof(token));
  if (!bootstrap_open(now))
    return Status::AuthenticationFailed;
  ++attempts_;
  const bool valid = equal(code, code_, 16);
  bootstrap_open(now);
  if (!valid)
    return Status::AuthenticationFailed;
  for (auto& session : sessions_) {
    if (now < session.expires)
      continue;
    mbedtls_platform_zeroize(&session, sizeof(session));
    const auto result = randomHex(random, token, 32);
    if (result != Status::Ok)
      return result;
    std::memcpy(session.token, token, sizeof(token));
    session.expires = now + kSessionSeconds;
    return Status::Ok;
  }
  return Status::TooLarge;
}
} // namespace core_security
