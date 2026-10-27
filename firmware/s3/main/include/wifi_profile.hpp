#pragma once

#include "core_network_policy.hpp"
#include "core_security.hpp"
#include "core_storage.hpp"
#include "wifi_codec.hpp"

#include <cstddef>
#include <cstdint>

namespace core_pilot {

struct SavedWifiNetwork final {
  std::uint8_t ssid_length{};
  std::uint8_t password_length{};
  char ssid[network_policy::kMaxSsidBytes + 1]{};
  char password[network_policy::kMaxPasswordBytes + 1]{};
};

struct SavedWifiNetworks final {
  SavedWifiNetwork networks[core_storage::kMaxSavedWifiNetworks]{};
  std::size_t count{};
  std::uint64_t generation{};

  SavedWifiNetworks() = default;
  SavedWifiNetworks(const SavedWifiNetworks&) = delete;
  SavedWifiNetworks& operator=(const SavedWifiNetworks&) = delete;
  ~SavedWifiNetworks();

  void clear();
  network_policy::Profile profile(std::size_t index) const;
  int find(const char* ssid, std::size_t ssid_length) const;
  bool upsert(const network_policy::Profile& candidate);
  bool forget(const char* ssid, std::size_t ssid_length);
};

core_security::Status loadWifi(core_security::StorageService& storage,
                               core_security::RandomSource& random, SavedWifiNetworks& saved);
core_security::Status saveWifi(core_security::StorageService& storage,
                               core_security::RandomSource& random, SavedWifiNetworks& saved);

} // namespace core_pilot
