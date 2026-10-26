#pragma once
#include "core_security.hpp"

namespace core_security {

enum class BootstrapState : uint8_t { open, consumed, expired, attempts_exhausted, unavailable };
enum class ExchangeResult : uint8_t { ok, incorrect, expired, attempts_exhausted, unavailable };

// Single owner or externally synchronized. Times are monotonic seconds.
class Auth final {
public:
  static constexpr uint64_t kBootstrapSeconds = 300;
  static constexpr uint64_t kSessionSeconds = 8ULL * 60 * 60;
  using BootstrapSink = void (*)(const char* code, uint64_t expires_at);
  ~Auth();
  Auth() = default;
  Auth(const Auth&) = delete;
  Auth& operator=(const Auth&) = delete;

  Status begin(RandomSource& random, uint64_t now, BootstrapSink physical_sink = nullptr);
  Status regenerate(RandomSource& random, uint64_t now, BootstrapSink physical_sink = nullptr);
  BootstrapState bootstrap_state(uint64_t now);
  uint64_t bootstrap_expires() const;
  void copy_bootstrap(char (&code)[17]) const;
  bool authorize(const char* token, uint64_t now);
  ExchangeResult exchange(const char* code, RandomSource& random, uint64_t now, char (&token)[65]);

private:
  struct Session {
    char token[65]{};
    uint64_t expires{};
  };
  void close_bootstrap(BootstrapState state);

  char code_[17]{};
  uint64_t bootstrap_expires_{};

  BootstrapState bootstrap_state_{BootstrapState::unavailable};
  Session sessions_[4]{};
};
} // namespace core_security
