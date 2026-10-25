#pragma once
#include "core_security.hpp"

namespace core_security {
// Single owner: HTTP task after boot initialization. Times are monotonic seconds.
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
  bool bootstrap_open(uint64_t now);
  bool authorize(const char* token, uint64_t now);
  Status exchange(const char* code, RandomSource& random, uint64_t now, char (&token)[65]);

private:
  struct Session {
    char token[65]{};
    uint64_t expires{};
  };
  char code_[17]{};
  uint64_t bootstrap_expires_{};
  unsigned attempts_{};
  Session sessions_[4]{};
};
} // namespace core_security
