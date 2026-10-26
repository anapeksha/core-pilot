#include "core_smtp.hpp"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mbedtls/platform_util.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace core_pilot::smtp {
namespace {

constexpr char kPath[] = "/core-pilot/config/smtp.enc";
constexpr std::uint16_t kSchema = 1;
constexpr std::uint8_t kMagic[] = {'S', 'M', 'T', '1'};
constexpr std::size_t kRecordOverhead = 64;
constexpr std::size_t kPayloadCapacity = 32 + kMaxHostBytes + kMaxUsernameBytes +
                                         kMaxPasswordBytes + (kMaxRecipients + 1) * kMaxEmailBytes;
constexpr std::size_t kWireBuffer = 1024;

struct Workspace final {
  Workspace() {
    plain = static_cast<std::uint8_t*>(
        heap_caps_malloc(kPayloadCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    encrypted = static_cast<std::uint8_t*>(
        heap_caps_malloc(kPayloadCapacity + kRecordOverhead, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  ~Workspace() {
    if (plain) {
      mbedtls_platform_zeroize(plain, kPayloadCapacity);
      heap_caps_free(plain);
    }
    if (encrypted) {
      mbedtls_platform_zeroize(encrypted, kPayloadCapacity + kRecordOverhead);
      heap_caps_free(encrypted);
    }
  }
  bool valid() const {
    return plain && encrypted;
  }
  std::uint8_t* plain{};
  std::uint8_t* encrypted{};
};

struct Lock final {
  explicit Lock(SemaphoreHandle_t value)
      : mutex(value), held(value && xSemaphoreTake(value, pdMS_TO_TICKS(1000)) == pdTRUE) {}
  ~Lock() {
    if (held)
      xSemaphoreGive(mutex);
  }
  SemaphoreHandle_t mutex{};
  bool held{};
};

Status map_storage(core_security::Status value) {
  switch (value) {
  case core_security::Status::Ok:
    return Status::Ok;
  case core_security::Status::NotFound:
    return Status::NotFound;
  case core_security::Status::TooLarge:
  case core_security::Status::BufferTooSmall:
    return Status::TooLarge;
  case core_security::Status::UnsupportedFormat:
    return Status::UnsupportedFormat;
  case core_security::Status::Invalid:
  case core_security::Status::AuthenticationFailed:
    return Status::CorruptData;
  case core_security::Status::IoError:
  case core_security::Status::CryptoError:
    return Status::StorageError;
  }
  return Status::StorageError;
}

bool put(std::uint8_t* out, std::size_t& pos, const void* data, std::size_t size) {
  if ((!data && size) || pos > kPayloadCapacity || size > kPayloadCapacity - pos)
    return false;
  if (size)
    std::memcpy(out + pos, data, size);
  pos += size;
  return true;
}
bool put8(std::uint8_t* out, std::size_t& pos, std::uint8_t value) {
  return put(out, pos, &value, 1);
}
bool put16(std::uint8_t* out, std::size_t& pos, std::uint16_t value) {
  const std::uint8_t bytes[] = {static_cast<std::uint8_t>(value),
                                static_cast<std::uint8_t>(value >> 8)};
  return put(out, pos, bytes, sizeof(bytes));
}
bool get(const std::uint8_t* in, std::size_t size, std::size_t& pos, void* out, std::size_t count) {
  if ((!out && count) || pos > size || count > size - pos)
    return false;
  if (count)
    std::memcpy(out, in + pos, count);
  pos += count;
  return true;
}
bool get8(const std::uint8_t* in, std::size_t size, std::size_t& pos, std::uint8_t& value) {
  return get(in, size, pos, &value, 1);
}
bool get16(const std::uint8_t* in, std::size_t size, std::size_t& pos, std::uint16_t& value) {
  std::uint8_t bytes[2]{};
  if (!get(in, size, pos, bytes, sizeof(bytes)))
    return false;
  value = static_cast<std::uint16_t>(bytes[0] | (static_cast<std::uint16_t>(bytes[1]) << 8));
  return true;
}
bool put_text(std::uint8_t* out, std::size_t& pos, const char* text, std::size_t maximum) {
  const std::size_t length = strnlen(text, maximum + 1);
  return length <= maximum && put16(out, pos, static_cast<std::uint16_t>(length)) &&
         put(out, pos, text, length);
}
bool get_text(const std::uint8_t* in, std::size_t size, std::size_t& pos, char* out,
              std::size_t maximum) {
  std::uint16_t length{};
  if (!get16(in, size, pos, length) || length > maximum || !get(in, size, pos, out, length))
    return false;
  out[length] = '\0';
  return true;
}

bool valid_text(const char* value, std::size_t maximum, bool required = false) {
  if (!value)
    return false;
  const std::size_t length = strnlen(value, maximum + 1);
  return length <= maximum && (!required || length != 0) && !contains_cr_or_lf(value, maximum);
}

bool valid_config(const Config& value) {
  if (static_cast<unsigned>(value.security) > static_cast<unsigned>(SecurityMode::PlainLanOnly) ||
      !valid_text(value.host, kMaxHostBytes) || !valid_text(value.username, kMaxUsernameBytes) ||
      !valid_text(value.from, kMaxEmailBytes) || value.recipient_count > kMaxRecipients ||
      value.password_length > kMaxPasswordBytes)
    return false;
  for (std::size_t i = 0; i < value.recipient_count; ++i)
    if (!valid_email_address(value.recipients[i]))
      return false;
  if (!value.enabled)
    return true;
  return transport_port_allowed(value.security, value.port) && value.host[0] != '\0' &&
         valid_email_address(value.from) && value.recipient_count != 0 &&
         (value.security != SecurityMode::PlainLanOnly || plain_lan_target_allowed(value.host));
}

bool encode_config(const Config& value, std::uint8_t* out, std::size_t& size) {
  size = 0;
  if (!put(out, size, kMagic, sizeof(kMagic)) || !put8(out, size, value.enabled ? 1 : 0) ||
      !put8(out, size, static_cast<std::uint8_t>(value.security)) ||
      !put16(out, size, value.port) || !put_text(out, size, value.host, kMaxHostBytes) ||
      !put_text(out, size, value.username, kMaxUsernameBytes) ||
      !put_text(out, size, value.from, kMaxEmailBytes) || !put8(out, size, value.recipient_count))
    return false;
  for (std::size_t i = 0; i < value.recipient_count; ++i)
    if (!put_text(out, size, value.recipients[i], kMaxEmailBytes))
      return false;
  return put16(out, size, value.password_length) &&
         put(out, size, value.password, value.password_length);
}

bool decode_config(const std::uint8_t* in, std::size_t size, Config& value) {
  std::size_t pos{};
  std::uint8_t enabled{}, security{};
  if (size < sizeof(kMagic) || std::memcmp(in, kMagic, sizeof(kMagic)) != 0)
    return false;
  pos = sizeof(kMagic);
  if (!get8(in, size, pos, enabled) || enabled > 1 || !get8(in, size, pos, security) ||
      security > static_cast<std::uint8_t>(SecurityMode::PlainLanOnly) ||
      !get16(in, size, pos, value.port) || !get_text(in, size, pos, value.host, kMaxHostBytes) ||
      !get_text(in, size, pos, value.username, kMaxUsernameBytes) ||
      !get_text(in, size, pos, value.from, kMaxEmailBytes) ||
      !get8(in, size, pos, value.recipient_count) || value.recipient_count > kMaxRecipients)
    return false;
  for (std::size_t i = 0; i < value.recipient_count; ++i)
    if (!get_text(in, size, pos, value.recipients[i], kMaxEmailBytes))
      return false;
  if (!get16(in, size, pos, value.password_length) || value.password_length > kMaxPasswordBytes ||
      !get(in, size, pos, value.password, value.password_length) || pos != size)
    return false;
  value.enabled = enabled != 0;
  value.security = static_cast<SecurityMode>(security);
  value.password_redacted = value.password_length != 0;
  return valid_config(value);
}

core_security::RecordSpec record_spec(std::uint64_t generation) {
  return {kPath,
          sizeof(kPath) - 1,
          core_security::Namespace::Configuration,
          core_security::RecordType::SmtpConfig,
          kSchema,
          generation};
}

struct Connection final {
  ~Connection() {
    if (tls)
      esp_tls_conn_destroy(tls);
    else if (fd >= 0)
      close(fd);
  }
  esp_tls_t* tls{};
  int fd{-1};
  bool secure{};
};

bool set_socket_timeout(int fd, std::uint32_t milliseconds) {
  timeval timeout{static_cast<time_t>(milliseconds / 1000),
                  static_cast<suseconds_t>((milliseconds % 1000) * 1000)};
  return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
         setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
}

Status connect_plain(const Config& config, const TransportOptions& options,
                     Connection& connection) {
  char port[6]{};
  std::snprintf(port, sizeof(port), "%u", static_cast<unsigned>(config.port));
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  addrinfo* addresses{};
  if (getaddrinfo(config.host, port, &hints, &addresses) != 0)
    return Status::ResolveFailed;
  bool policy_denied = false;
  for (addrinfo* address = addresses; address; address = address->ai_next) {
    const void* raw_address = nullptr;
    std::size_t raw_size = 0;
    if (address->ai_family == AF_INET) {
      raw_address = &reinterpret_cast<const sockaddr_in*>(address->ai_addr)->sin_addr;
      raw_size = sizeof(in_addr);
    } else if (address->ai_family == AF_INET6) {
      raw_address = &reinterpret_cast<const sockaddr_in6*>(address->ai_addr)->sin6_addr;
      raw_size = sizeof(in6_addr);
    }
    if (raw_address == nullptr ||
        !resolved_target_allowed(config.security, address->ai_family,
                                 static_cast<const std::uint8_t*>(raw_address), raw_size)) {
      policy_denied = true;
      continue;
    }
    const int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (fd < 0)
      continue;
    if (set_socket_timeout(fd, options.connect_timeout_ms) &&
        connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
      connection.fd = fd;
      break;
    }
    close(fd);
  }
  freeaddrinfo(addresses);
  if (connection.fd < 0)
    return policy_denied ? Status::PolicyDenied : Status::ConnectFailed;
  if (!set_socket_timeout(connection.fd, options.io_timeout_ms))
    return Status::IoError;
  return Status::Ok;
}

Status enable_tls(const Config& config, const TransportOptions& options, Connection& connection,
                  bool existing_socket) {
  esp_tls_cfg_t cfg{};
  cfg.timeout_ms = static_cast<int>(options.connect_timeout_ms);
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.common_name = config.host;
  cfg.skip_common_name = false;
  esp_tls_t* tls = esp_tls_init();
  if (!tls)
    return Status::TlsFailed;
  if (existing_socket) {
    if (esp_tls_set_conn_sockfd(tls, connection.fd) != ESP_OK ||
        esp_tls_set_conn_state(tls, ESP_TLS_CONNECTING) != ESP_OK) {
      esp_tls_conn_destroy(tls);
      return Status::TlsFailed;
    }
  }
  const int result = esp_tls_conn_new_sync(config.host, static_cast<int>(std::strlen(config.host)),
                                           config.port, &cfg, tls);
  if (result != 1) {
    esp_tls_conn_destroy(tls);
    if (existing_socket)
      connection.fd = -1;
    return result == 0 ? Status::Timeout : Status::TlsFailed;
  }
  connection.tls = tls;
  connection.secure = true;
  connection.fd = -1;
  int fd{};
  if (esp_tls_get_conn_sockfd(tls, &fd) != ESP_OK || !set_socket_timeout(fd, options.io_timeout_ms))
    return Status::IoError;
  return Status::Ok;
}

ssize_t connection_read(Connection& connection, void* output, std::size_t size) {
  return connection.tls ? esp_tls_conn_read(connection.tls, output, size)
                        : recv(connection.fd, output, size, 0);
}
ssize_t connection_write(Connection& connection, const void* input, std::size_t size) {
  return connection.tls ? esp_tls_conn_write(connection.tls, input, size)
                        : ::send(connection.fd, input, size, 0);
}

Status write_all(Connection& connection, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  std::size_t offset{};
  while (offset < size) {
    const ssize_t result = connection_write(connection, bytes + offset, size - offset);
    if (result <= 0)
      return (errno == EAGAIN || errno == EWOULDBLOCK) ? Status::Timeout : Status::IoError;
    offset += static_cast<std::size_t>(result);
  }
  return Status::Ok;
}

Status read_reply(Connection& connection, int& code) {
  ReplyParser parser;
  std::uint8_t buffer[256]{};
  while (true) {
    const ssize_t count = connection_read(connection, buffer, sizeof(buffer));
    if (count <= 0)
      return (errno == EAGAIN || errno == EWOULDBLOCK) ? Status::Timeout : Status::IoError;
    const auto result = parser.consume(buffer, static_cast<std::size_t>(count));
    if (result == ReplyParser::Result::Complete) {
      code = parser.code();
      return Status::Ok;
    }
    if (result != ReplyParser::Result::NeedMore)
      return Status::ProtocolError;
  }
}

Status command(Connection& connection, const char* text, std::size_t length, int expected_a,
               int expected_b = 0) {
  Status result = write_all(connection, text, length);
  if (result != Status::Ok)
    return result;
  int code{};
  result = read_reply(connection, code);
  if (result != Status::Ok)
    return result;
  return (code == expected_a || (expected_b && code == expected_b)) ? Status::Ok
                                                                    : Status::ProtocolError;
}

std::size_t base64(const std::uint8_t* input, std::size_t length, char* output,
                   std::size_t capacity) {
  constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const std::size_t needed = ((length + 2) / 3) * 4;
  if (needed + 1 > capacity)
    return 0;
  std::size_t in{}, out{};
  while (in < length) {
    const std::size_t remaining = length - in;
    const std::uint32_t a = input[in++];
    const std::uint32_t b = remaining > 1 ? input[in++] : 0;
    const std::uint32_t c = remaining > 2 ? input[in++] : 0;
    const std::uint32_t value = (a << 16) | (b << 8) | c;
    output[out++] = alphabet[(value >> 18) & 63];
    output[out++] = alphabet[(value >> 12) & 63];
    output[out++] = remaining > 1 ? alphabet[(value >> 6) & 63] : '=';
    output[out++] = remaining > 2 ? alphabet[value & 63] : '=';
  }
  output[out] = '\0';
  return out;
}

Status authenticate(Connection& connection, const Config& config) {
  if (config.username[0] == '\0' && config.password_length == 0)
    return Status::Ok;
  if (!connection.secure)
    return Status::PolicyDenied;
  char encoded_user[((kMaxUsernameBytes + 2) / 3) * 4 + 1]{};
  char encoded_password[((kMaxPasswordBytes + 2) / 3) * 4 + 1]{};
  struct Wipe final {
    char* a;
    std::size_t a_size;
    char* b;
    std::size_t b_size;
    ~Wipe() {
      mbedtls_platform_zeroize(a, a_size);
      mbedtls_platform_zeroize(b, b_size);
    }
  } wipe{encoded_user, sizeof(encoded_user), encoded_password, sizeof(encoded_password)};
  const std::size_t user_size =
      base64(reinterpret_cast<const std::uint8_t*>(config.username), std::strlen(config.username),
             encoded_user, sizeof(encoded_user));
  const std::size_t password_size =
      base64(config.password, config.password_length, encoded_password, sizeof(encoded_password));
  if (!user_size || (!password_size && config.password_length))
    return Status::TooLarge;
  Status result = command(connection, "AUTH LOGIN\r\n", 12, 334);
  if (result != Status::Ok)
    return Status::AuthenticationFailed;
  char line[sizeof(encoded_user) + 3]{};
  std::memcpy(line, encoded_user, user_size);
  std::memcpy(line + user_size, "\r\n", 2);
  result = command(connection, line, user_size + 2, 334);
  mbedtls_platform_zeroize(line, sizeof(line));
  if (result != Status::Ok)
    return Status::AuthenticationFailed;
  char password_line[sizeof(encoded_password) + 3]{};
  std::memcpy(password_line, encoded_password, password_size);
  std::memcpy(password_line + password_size, "\r\n", 2);
  result = command(connection, password_line, password_size + 2, 235);
  mbedtls_platform_zeroize(password_line, sizeof(password_line));
  return result == Status::Ok ? Status::Ok : Status::AuthenticationFailed;
}

Status send_envelope(Connection& connection, const Config& config, const Message& message) {
  char line[kMaxEmailBytes + 32]{};
  int length = std::snprintf(line, sizeof(line), "MAIL FROM:<%s>\r\n", config.from);
  if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(line))
    return Status::TooLarge;
  Status result = command(connection, line, static_cast<std::size_t>(length), 250);
  if (result != Status::Ok)
    return Status::MessageRejected;
  const bool overrides = message.recipient_count != 0;
  const std::size_t count = overrides ? message.recipient_count : config.recipient_count;
  for (std::size_t i = 0; i < count; ++i) {
    const char* recipient = overrides ? message.recipients[i] : config.recipients[i];
    length = std::snprintf(line, sizeof(line), "RCPT TO:<%s>\r\n", recipient);
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(line))
      return Status::TooLarge;
    result = command(connection, line, static_cast<std::size_t>(length), 250, 251);
    if (result != Status::Ok)
      return Status::RecipientRejected;
  }
  return Status::Ok;
}

} // namespace

struct ConfigRepository::State final {
  StaticSemaphore_t mutex_buffer{};
  SemaphoreHandle_t mutex{};
};

void clear_config(Config& config) {
  mbedtls_platform_zeroize(&config, sizeof(config));
}

bool contains_cr_or_lf(const char* value, std::size_t maximum) {
  if (!value)
    return true;
  const std::size_t length = strnlen(value, maximum + 1);
  if (length > maximum)
    return true;
  for (std::size_t i = 0; i < length; ++i)
    if (value[i] == '\r' || value[i] == '\n')
      return true;
  return false;
}

bool valid_email_address(const char* value) {
  if (!valid_text(value, kMaxEmailBytes, true))
    return false;
  const char* at = std::strchr(value, '@');
  if (!at || at == value || std::strchr(at + 1, '@') || at[1] == '\0')
    return false;
  const char* domain = at + 1;
  const std::size_t length = std::strlen(value);
  if (!std::strchr(domain, '.') || domain[0] == '.' || value[length - 1] == '.')
    return false;
  for (const char* cursor = value; *cursor; ++cursor)
    if (*cursor == ' ' || *cursor == '\t' || static_cast<unsigned char>(*cursor) < 0x20U)
      return false;
  return true;
}

bool same_credential_endpoint(const Config& current, const Config& update) {
  return current.port == update.port && current.security == update.security &&
         std::strcmp(current.host, update.host) == 0 &&
         std::strcmp(current.username, update.username) == 0;
}

bool transport_port_allowed(SecurityMode security, std::uint16_t port) {
  return (security == SecurityMode::Tls && port == 465) ||
         (security == SecurityMode::StartTls && port == 587) ||
         (security == SecurityMode::PlainLanOnly && port == 25);
}

bool resolved_target_allowed(SecurityMode security, int address_family, const std::uint8_t* address,
                             std::size_t address_size) {
  if (address == nullptr)
    return false;
  if (address_family == AF_INET && address_size == sizeof(in_addr)) {
    in_addr parsed{};
    std::memcpy(&parsed, address, sizeof(parsed));
    const std::uint32_t value = ntohl(parsed.s_addr);
    const bool private_lan = (value & 0xff000000U) == 0x0a000000U ||
                             (value & 0xfff00000U) == 0xac100000U ||
                             (value & 0xffff0000U) == 0xc0a80000U;
    if (security == SecurityMode::PlainLanOnly)
      return private_lan;
    const bool prohibited =
        value == 0 || (value & 0xff000000U) == 0x00000000U || private_lan ||
        (value & 0xffc00000U) == 0x64400000U || (value & 0xff000000U) == 0x7f000000U ||
        (value & 0xffff0000U) == 0xa9fe0000U || (value & 0xffffff00U) == 0xc0000000U ||
        (value & 0xffffff00U) == 0xc0000200U || (value & 0xfffe0000U) == 0xc6120000U ||
        (value & 0xffffff00U) == 0xc6336400U || (value & 0xffffff00U) == 0xcb007100U ||
        (value & 0xf0000000U) == 0xe0000000U;
    return !prohibited;
  }
  if (address_family == AF_INET6 && address_size == sizeof(in6_addr)) {
    in6_addr parsed{};
    std::memcpy(&parsed, address, sizeof(parsed));
    const auto* bytes = parsed.s6_addr;
    const bool unique_local = (bytes[0] & 0xfeU) == 0xfcU;
    if (security == SecurityMode::PlainLanOnly)
      return unique_local;
    const bool global_unicast = (bytes[0] & 0xe0U) == 0x20U;
    const bool documentation =
        bytes[0] == 0x20U && bytes[1] == 0x01U && bytes[2] == 0x0dU && bytes[3] == 0xb8U;
    return global_unicast && !documentation;
  }
  return false;
}

bool plain_lan_target_allowed(const char* host) {
  if (!host || contains_cr_or_lf(host, kMaxHostBytes))
    return false;
  in_addr address4{};
  if (inet_pton(AF_INET, host, &address4) == 1)
    return resolved_target_allowed(SecurityMode::PlainLanOnly, AF_INET,
                                   reinterpret_cast<const std::uint8_t*>(&address4),
                                   sizeof(address4));
  in6_addr address6{};
  return inet_pton(AF_INET6, host, &address6) == 1 &&
         resolved_target_allowed(SecurityMode::PlainLanOnly, AF_INET6, address6.s6_addr,
                                 sizeof(address6));
}

ReplyParser::Result ReplyParser::consume(const std::uint8_t* bytes, std::size_t size) {
  if ((!bytes && size) || complete_)
    return Result::Invalid;
  for (std::size_t i = 0; i < size; ++i) {
    if (++total_ > kMaxReplyBytes)
      return Result::TooLarge;
    const char byte = static_cast<char>(bytes[i]);
    if (byte == '\0')
      return Result::Invalid;
    if (saw_cr_) {
      if (byte != '\n')
        return Result::Invalid;
      saw_cr_ = false;
      if (line_length_ < 4 || line_[0] < '0' || line_[0] > '5' || line_[1] < '0' ||
          line_[1] > '9' || line_[2] < '0' || line_[2] > '9' ||
          (line_[3] != ' ' && line_[3] != '-'))
        return Result::Invalid;
      const int line_code = (line_[0] - '0') * 100 + (line_[1] - '0') * 10 + line_[2] - '0';
      if (!have_code_) {
        code_ = line_code;
        have_code_ = true;
      } else if (line_code != code_) {
        return Result::Invalid;
      }
      const bool final_line = line_[3] == ' ';
      line_length_ = 0;
      if (final_line) {
        complete_ = true;
        return i + 1 == size ? Result::Complete : Result::Invalid;
      }
      continue;
    }
    if (byte == '\r') {
      saw_cr_ = true;
      continue;
    }
    if (byte == '\n' || line_length_ >= kMaxReplyLineBytes)
      return line_length_ >= kMaxReplyLineBytes ? Result::TooLarge : Result::Invalid;
    line_[line_length_++] = byte;
  }
  return Result::NeedMore;
}

void ReplyParser::reset() {
  mbedtls_platform_zeroize(this, sizeof(*this));
}

Status encode_data_body(const char* body, std::size_t body_length, std::uint8_t* output,
                        std::size_t capacity, std::size_t& written) {
  written = 0;
  if ((!body && body_length) || (!output && capacity) || body_length > kMaxBodyBytes)
    return Status::InvalidArgument;
  bool line_start = true;
  for (std::size_t i = 0; i < body_length; ++i) {
    char byte = body[i];
    if (byte == '\0')
      return Status::InvalidArgument;
    if (line_start && byte == '.') {
      if (written >= capacity)
        return Status::TooLarge;
      output[written++] = '.';
    }
    if (byte == '\r') {
      if (i + 1 < body_length && body[i + 1] == '\n')
        ++i;
      if (capacity - written < 2)
        return Status::TooLarge;
      output[written++] = '\r';
      output[written++] = '\n';
      line_start = true;
    } else if (byte == '\n') {
      if (capacity - written < 2)
        return Status::TooLarge;
      output[written++] = '\r';
      output[written++] = '\n';
      line_start = true;
    } else {
      if (written >= capacity)
        return Status::TooLarge;
      output[written++] = static_cast<std::uint8_t>(byte);
      line_start = false;
    }
  }
  if (written < 2 || output[written - 2] != '\r' || output[written - 1] != '\n') {
    if (capacity - written < 2)
      return Status::TooLarge;
    output[written++] = '\r';
    output[written++] = '\n';
  }
  return Status::Ok;
}

ConfigRepository::ConfigRepository(core_security::StorageService& storage,
                                   core_security::RandomSource& random)
    : storage_(storage), random_(random) {}
ConfigRepository::~ConfigRepository() {
  if (state_) {
    if (state_->mutex)
      vSemaphoreDelete(state_->mutex);
    mbedtls_platform_zeroize(state_, sizeof(*state_));
    heap_caps_free(state_);
  }
}
Status ConfigRepository::start() {
  if (state_)
    return Status::Ok;
  auto* state = static_cast<State*>(heap_caps_calloc(1, sizeof(State), MALLOC_CAP_INTERNAL));
  if (!state)
    return Status::StorageError;
  state->mutex = xSemaphoreCreateMutexStatic(&state->mutex_buffer);
  if (!state->mutex) {
    heap_caps_free(state);
    return Status::StorageError;
  }
  state_ = state;
  return Status::Ok;
}
Status ConfigRepository::load(Config& output) {
  clear_config(output);
  if (!state_)
    return Status::NotReady;
  Lock lock(state_->mutex);
  if (!lock.held)
    return Status::StorageError;
  Workspace workspace;
  if (!workspace.valid())
    return Status::StorageError;
  std::size_t size{};
  core_security::Metadata metadata{};
  Status result = map_storage(storage_.get(record_spec(0), workspace.encrypted,
                                           kPayloadCapacity + kRecordOverhead, workspace.plain,
                                           kPayloadCapacity, size, metadata));
  if (result != Status::Ok)
    return result;
  if (metadata.schema_version != kSchema)
    return Status::UnsupportedFormat;
  if (!decode_config(workspace.plain, size, output)) {
    clear_config(output);
    return Status::CorruptData;
  }
  return Status::Ok;
}
Status ConfigRepository::save(Config& config) {
  struct Wipe final {
    Config& value;
    ~Wipe() {
      mbedtls_platform_zeroize(value.password, sizeof(value.password));
      value.password_length = 0;
    }
  } wipe{config};
  if (!state_)
    return Status::NotReady;
  if (!valid_config(config))
    return config.security == SecurityMode::PlainLanOnly && !plain_lan_target_allowed(config.host)
               ? Status::PolicyDenied
               : Status::InvalidArgument;
  Lock lock(state_->mutex);
  if (!lock.held)
    return Status::StorageError;
  Workspace workspace;
  if (!workspace.valid())
    return Status::StorageError;
  std::uint64_t generation{};
  if (config.password_redacted && config.password_length == 0) {
    Config old{};
    struct Clear final {
      Config& value;
      ~Clear() {
        clear_config(value);
      }
    } clear{old};
    std::size_t old_size{};
    core_security::Metadata metadata{};
    const Status loaded = map_storage(
        storage_.get(record_spec(0), workspace.encrypted, kPayloadCapacity + kRecordOverhead,
                     workspace.plain, kPayloadCapacity, old_size, metadata));
    if (loaded == Status::Ok) {
      if (metadata.schema_version != kSchema || !decode_config(workspace.plain, old_size, old))
        return Status::CorruptData;
      if (!same_credential_endpoint(old, config))
        return Status::InvalidArgument;
      generation = metadata.generation;
      config.password_length = old.password_length;
      std::memcpy(config.password, old.password, old.password_length);
    } else if (loaded != Status::NotFound) {
      return loaded;
    }
  } else {
    std::size_t ignored{};
    core_security::Metadata metadata{};
    const Status loaded = map_storage(
        storage_.get(record_spec(0), workspace.encrypted, kPayloadCapacity + kRecordOverhead,
                     workspace.plain, kPayloadCapacity, ignored, metadata));
    if (loaded == Status::Ok)
      generation = metadata.generation;
    else if (loaded != Status::NotFound)
      return loaded;
  }
  std::size_t size{};
  if (!encode_config(config, workspace.plain, size))
    return Status::TooLarge;
  return map_storage(storage_.put(record_spec(generation + 1), workspace.plain, size, random_,
                                  workspace.encrypted, kPayloadCapacity + kRecordOverhead));
}

Status send(const Config& config, const Message& message, const TransportOptions& options) {
  if (!config.enabled || !valid_config(config) ||
      !valid_text(message.subject, kMaxSubjectBytes, true) || !message.body ||
      message.body_length == 0 || message.body_length > kMaxBodyBytes ||
      message.recipient_count > kMaxRecipients || !valid_text(options.client_name, 253, true) ||
      options.connect_timeout_ms == 0 || options.io_timeout_ms == 0)
    return Status::InvalidArgument;
  for (std::size_t i = 0; i < message.recipient_count; ++i)
    if (!valid_email_address(message.recipients[i]))
      return Status::InvalidArgument;
  if (!transport_port_allowed(config.security, config.port) ||
      (config.security == SecurityMode::PlainLanOnly && !plain_lan_target_allowed(config.host)))
    return Status::PolicyDenied;

  Connection connection;
  Status result = connect_plain(config, options, connection);
  if (result != Status::Ok)
    return result;
  if (config.security == SecurityMode::Tls) {
    result = enable_tls(config, options, connection, true);
    if (result != Status::Ok)
      return result;
  }
  int reply{};
  result = read_reply(connection, reply);
  if (result != Status::Ok || reply != 220)
    return Status::ProtocolError;
  char greeting[320]{};
  const int greeting_length =
      std::snprintf(greeting, sizeof(greeting), "EHLO %s\r\n", options.client_name);
  if (greeting_length <= 0 || static_cast<std::size_t>(greeting_length) >= sizeof(greeting))
    return Status::InvalidArgument;
  result = command(connection, greeting, static_cast<std::size_t>(greeting_length), 250);
  if (result != Status::Ok)
    return result;
  if (config.security == SecurityMode::StartTls) {
    result = command(connection, "STARTTLS\r\n", 10, 220);
    if (result != Status::Ok)
      return Status::TlsFailed;
    result = enable_tls(config, options, connection, true);
    if (result != Status::Ok)
      return result;
    result = command(connection, greeting, static_cast<std::size_t>(greeting_length), 250);
    if (result != Status::Ok)
      return result;
  }
  result = authenticate(connection, config);
  if (result != Status::Ok)
    return result;
  result = send_envelope(connection, config, message);
  if (result != Status::Ok)
    return result;
  result = command(connection, "DATA\r\n", 6, 354);
  if (result != Status::Ok)
    return Status::MessageRejected;

  char headers[kMaxEmailBytes + kMaxSubjectBytes + 128]{};
  const int header_length = std::snprintf(headers, sizeof(headers),
                                          "From: <%s>\r\nSubject: %s\r\nMIME-Version: "
                                          "1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\n",
                                          config.from, message.subject);
  if (header_length <= 0 || static_cast<std::size_t>(header_length) >= sizeof(headers))
    return Status::TooLarge;
  result = write_all(connection, headers, static_cast<std::size_t>(header_length));
  if (result != Status::Ok)
    return result;
  auto* encoded = static_cast<std::uint8_t*>(
      heap_caps_malloc(kMaxBodyBytes * 2 + 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!encoded)
    return Status::IoError;
  std::size_t encoded_size{};
  result = encode_data_body(message.body, message.body_length, encoded, kMaxBodyBytes * 2 + 4,
                            encoded_size);
  if (result == Status::Ok)
    result = write_all(connection, encoded, encoded_size);
  mbedtls_platform_zeroize(encoded, kMaxBodyBytes * 2 + 4);
  heap_caps_free(encoded);
  if (result != Status::Ok)
    return result;
  result = command(connection, ".\r\n", 3, 250);
  if (result != Status::Ok)
    return Status::MessageRejected;
  (void)command(connection, "QUIT\r\n", 6, 221);
  return Status::Ok;
}

} // namespace core_pilot::smtp
