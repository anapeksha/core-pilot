#pragma once

#include "core_security.hpp"

#include <cstddef>
#include <cstdint>

namespace core_storage {
// Wi-Fi schema 1 uses the stable postcard-compatible wire payload:
// WifiCredentials { String ssid, String password }, Option<String> hostname.
// Fields are byte spans, never null-terminated or printed. Callers keep all
// buffers private and wipe them after use. Encrypted CPER I/O uses
// StorageService.
struct WifiConfig {
  const uint8_t* ssid;
  size_t ssid_len;
  const uint8_t* password;
  size_t password_len;
  const uint8_t* hostname; // nullptr = None; non-null = Some
  size_t hostname_len;
};
constexpr uint16_t kWifiSchema = 1;
constexpr size_t kMaxWifiPayload = 1 + 32 + 1 + 63 + 1 + 1 + 63;
core_security::Status encode_wifi(const WifiConfig& config, uint8_t* out, size_t capacity,
                                  size_t& written);
// Returned spans borrow the plaintext input. Refuse mismatched schema instead
// of treating newer/older payloads as credentials.
core_security::Status decode_wifi(uint16_t schema, const uint8_t* input, size_t length,
                                  WifiConfig& config);
} // namespace core_storage
