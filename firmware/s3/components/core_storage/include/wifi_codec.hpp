#pragma once

#include "core_security.hpp"

#include <cstddef>
#include <cstdint>

namespace core_storage {

constexpr std::size_t kMaxSavedWifiNetworks = 8;
constexpr std::uint16_t kWifiSchema1 = 1;
constexpr std::uint16_t kWifiSchema = 2;

struct WifiConfig {
  const std::uint8_t* ssid;
  std::size_t ssid_len;
  const std::uint8_t* password;
  std::size_t password_len;
  const std::uint8_t* hostname;
  std::size_t hostname_len;
};

struct WifiNetwork {
  const std::uint8_t* ssid;
  std::size_t ssid_len;
  const std::uint8_t* password;
  std::size_t password_len;
};

struct WifiNetworks {
  WifiNetwork networks[kMaxSavedWifiNetworks]{};
  std::size_t count{};
};

constexpr std::size_t kMaxWifiSchema1Payload = 1 + 32 + 1 + 63 + 1 + 1 + 63;
constexpr std::size_t kMaxWifiPayload = 1 + kMaxSavedWifiNetworks * (1 + 32 + 1 + 63);

// Schema 1 compatibility codec. New records must use encode_wifi_networks().
core_security::Status encode_wifi(const WifiConfig& config, std::uint8_t* out, std::size_t capacity,
                                  std::size_t& written);
core_security::Status decode_wifi(std::uint16_t schema, const std::uint8_t* input,
                                  std::size_t length, WifiConfig& config);

// Schema 2 stores networks in preference order. Returned spans borrow input.
core_security::Status encode_wifi_networks(const WifiNetworks& config, std::uint8_t* out,
                                           std::size_t capacity, std::size_t& written);
core_security::Status decode_wifi_networks(std::uint16_t schema, const std::uint8_t* input,
                                           std::size_t length, WifiNetworks& config);

} // namespace core_storage
