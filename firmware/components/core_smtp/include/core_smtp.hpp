#pragma once

#include "core_security.hpp"

#include <cstddef>
#include <cstdint>

namespace core_pilot::smtp {

inline constexpr std::size_t kMaxHostBytes = 253;
inline constexpr std::size_t kMaxUsernameBytes = 128;
inline constexpr std::size_t kMaxPasswordBytes = 256;
inline constexpr std::size_t kMaxEmailBytes = 254;
inline constexpr std::size_t kMaxSubjectBytes = 160;
inline constexpr std::size_t kMaxBodyBytes = 4096;
inline constexpr std::size_t kMaxRecipients = 4;
inline constexpr std::size_t kMaxReplyLineBytes = 512;
inline constexpr std::size_t kMaxReplyBytes = 4096;

// TLS is implicit TLS. StartTls requires a successful STARTTLS upgrade.
// PlainLanOnly is deliberately restricted to literal RFC1918/IPv6 local addresses.
enum class SecurityMode : std::uint8_t { StartTls, Tls, PlainLanOnly };

enum class Status : std::uint8_t {
  Ok,
  NotFound,
  InvalidArgument,
  TooLarge,
  NotReady,
  StorageError,
  CorruptData,
  UnsupportedFormat,
  PolicyDenied,
  ResolveFailed,
  ConnectFailed,
  TlsFailed,
  Timeout,
  IoError,
  ProtocolError,
  AuthenticationFailed,
  RecipientRejected,
  MessageRejected,
};

struct Config final {
  bool enabled{};
  SecurityMode security{SecurityMode::StartTls};
  std::uint16_t port{};
  char host[kMaxHostBytes + 1]{};
  char username[kMaxUsernameBytes + 1]{};
  char from[kMaxEmailBytes + 1]{};
  std::uint8_t recipient_count{};
  char recipients[kMaxRecipients][kMaxEmailBytes + 1]{};
  bool password_redacted{};
  std::uint16_t password_length{};
  std::uint8_t password[kMaxPasswordBytes]{};
};

struct Message final {
  char subject[kMaxSubjectBytes + 1]{};
  const char* body{};
  std::size_t body_length{};
  std::uint8_t recipient_count{};
  char recipients[kMaxRecipients][kMaxEmailBytes + 1]{};
};

void clear_config(Config& config);

// Incremental bounded parser for RFC 5321 single and multiline replies.
class ReplyParser final {
public:
  enum class Result : std::uint8_t { NeedMore, Complete, Invalid, TooLarge };
  Result consume(const std::uint8_t* bytes, std::size_t size);
  void reset();
  int code() const {
    return code_;
  }

private:
  std::size_t total_{};
  std::size_t line_length_{};
  int code_{};
  bool have_code_{};
  bool saw_cr_{};
  bool complete_{};
  char line_[kMaxReplyLineBytes + 1]{};
};

bool contains_cr_or_lf(const char* value, std::size_t maximum);
bool valid_email_address(const char* value);
bool plain_lan_target_allowed(const char* host);

// Writes an SMTP DATA body with CRLF normalization and dot-stuffing. The
// returned bytes always end in CRLF but do not include the final ".\r\n".
Status encode_data_body(const char* body, std::size_t body_length, std::uint8_t* output,
                        std::size_t capacity, std::size_t& written);

class ConfigRepository final {
public:
  ConfigRepository(core_security::StorageService& storage, core_security::RandomSource& random);
  ~ConfigRepository();
  ConfigRepository(const ConfigRepository&) = delete;
  ConfigRepository& operator=(const ConfigRepository&) = delete;

  Status start();
  Status load(Config& output);
  // Consumes and wipes config.password on every return path. If password_redacted
  // is true and password_length is zero, the existing encrypted password is retained.
  Status save(Config& config);

private:
  struct State;
  State* state_{};
  core_security::StorageService& storage_;
  core_security::RandomSource& random_;
};

struct TransportOptions final {
  std::uint32_t connect_timeout_ms{10000};
  std::uint32_t io_timeout_ms{10000};
  const char* client_name{"core-pilot.local"};
};

// Blocking and bounded. Call only from a future core-0 SMTP owner task, never
// directly from an HTTP callback. No input, credential, or message data is logged.
Status send(const Config& config, const Message& message,
            const TransportOptions& options = TransportOptions{});

} // namespace core_pilot::smtp
