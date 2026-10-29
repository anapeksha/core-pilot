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

void Auth::close_bootstrap(BootstrapState state) {
  mbedtls_platform_zeroize(code_, sizeof(code_));
  bootstrap_expires_ = 0;
  failed_attempts_ = 0;
  bootstrap_state_ = state;
}

Status Auth::begin(RandomSource& random, uint64_t now, BootstrapSink sink) {
  mbedtls_platform_zeroize(sessions_, sizeof(sessions_));
  return regenerate(random, now, sink);
}

Status Auth::regenerate(RandomSource& random, uint64_t now, BootstrapSink sink) {
  close_bootstrap(BootstrapState::unavailable);
  mbedtls_platform_zeroize(sessions_, sizeof(sessions_));
  const auto result = randomHex(random, code_, 8);
  if (result != Status::Ok)
    return result;
  bootstrap_expires_ = now + kBootstrapSeconds;
  bootstrap_state_ = BootstrapState::open;
  if (sink)
    sink(code_, bootstrap_expires_);
  return Status::Ok;
}

BootstrapState Auth::bootstrap_state(uint64_t now) {
  if ((bootstrap_state_ == BootstrapState::open ||
       bootstrap_state_ == BootstrapState::attempts_exhausted) &&
      now >= bootstrap_expires_) {
    close_bootstrap(BootstrapState::expired);
  }
  return bootstrap_state_;
}

uint64_t Auth::bootstrap_expires() const {
  return bootstrap_expires_;
}

void Auth::copy_bootstrap(char (&code)[17]) const {
  mbedtls_platform_zeroize(code, sizeof(code));
  if (bootstrap_state_ == BootstrapState::open)
    std::memcpy(code, code_, sizeof(code));
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

ExchangeResult Auth::exchange(const char* code, RandomSource& random, uint64_t now,
                              char (&token)[65]) {
  mbedtls_platform_zeroize(token, sizeof(token));
  const auto state = bootstrap_state(now);
  if (state == BootstrapState::expired)
    return ExchangeResult::expired;
  if (state == BootstrapState::attempts_exhausted)
    return ExchangeResult::attempts_exhausted;
  if (state != BootstrapState::open)
    return ExchangeResult::unavailable;

  if (!equal(code, code_, 16)) {
    ++failed_attempts_;
    if (failed_attempts_ >= kBootstrapAttemptLimit) {
      bootstrap_state_ = BootstrapState::attempts_exhausted;
      return ExchangeResult::attempts_exhausted;
    }
    return ExchangeResult::incorrect;
  }

  for (auto& session : sessions_) {
    if (now < session.expires)
      continue;
    mbedtls_platform_zeroize(&session, sizeof(session));
    if (randomHex(random, token, 32) != Status::Ok) {
      close_bootstrap(BootstrapState::unavailable);
      return ExchangeResult::unavailable;
    }
    std::memcpy(session.token, token, sizeof(token));
    session.expires = now + kSessionSeconds;
    close_bootstrap(BootstrapState::consumed);
    return ExchangeResult::ok;
  }
  return ExchangeResult::unavailable;
}
} // namespace core_security
