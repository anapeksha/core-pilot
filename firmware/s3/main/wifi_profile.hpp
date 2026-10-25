#pragma once

#include "core_network_policy.hpp"
#include "core_security.hpp"
#include "core_storage.hpp"
#include "wifi_codec.hpp"

#include <cstddef>
#include <cstdint>

namespace core_pilot {

struct LoadedWifi final {
  std::uint8_t plaintext[core_storage::kMaxWifiPayload]{};
  std::uint8_t encrypted[256]{};
  network_policy::Profile profile{};

  LoadedWifi() = default;
  LoadedWifi(const LoadedWifi&) = delete;
  LoadedWifi& operator=(const LoadedWifi&) = delete;
  ~LoadedWifi();

  void clear();
};

core_security::Status loadWifi(core_security::StorageService& storage, LoadedWifi& loaded);
core_security::Status saveWifi(core_security::StorageService& storage,
                               core_security::RandomSource& random, const char* ssid,
                               std::size_t ssid_length, const char* password,
                               std::size_t password_length);

} // namespace core_pilot
