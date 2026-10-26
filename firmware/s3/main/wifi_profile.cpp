#include "wifi_profile.hpp"

#include "mbedtls/platform_util.h"

#include <cstring>

namespace core_pilot {
namespace {
constexpr char kWifiPath[] = "/core-pilot/config/wifi.enc";
constexpr std::size_t kEncryptedCapacity = core_storage::kMaxWifiPayload + 128;

void copyNetwork(SavedWifiNetwork& destination, const core_storage::WifiNetwork& source) {
  destination.ssid_length = static_cast<std::uint8_t>(source.ssid_len);
  destination.password_length = static_cast<std::uint8_t>(source.password_len);
  std::memcpy(destination.ssid, source.ssid, source.ssid_len);
  if (source.password_len != 0)
    std::memcpy(destination.password, source.password, source.password_len);
}
} // namespace

SavedWifiNetworks::~SavedWifiNetworks() {
  clear();
}

void SavedWifiNetworks::clear() {
  mbedtls_platform_zeroize(networks, sizeof(networks));
  count = 0;
  generation = 0;
}

network_policy::Profile SavedWifiNetworks::profile(std::size_t index) const {
  if (index >= count)
    return {};
  const auto& network = networks[index];
  return {network.ssid, network.ssid_length, network.password, network.password_length};
}

int SavedWifiNetworks::find(const char* ssid, std::size_t ssid_length) const {
  if (ssid == nullptr || ssid_length == 0 || ssid_length > network_policy::kMaxSsidBytes)
    return -1;
  for (std::size_t i = 0; i < count; ++i) {
    if (networks[i].ssid_length == ssid_length &&
        std::memcmp(networks[i].ssid, ssid, ssid_length) == 0)
      return static_cast<int>(i);
  }
  return -1;
}

bool SavedWifiNetworks::upsert(const network_policy::Profile& candidate) {
  if (!network_policy::valid_profile(candidate))
    return false;
  int existing = find(candidate.ssid, candidate.ssid_length);
  std::size_t index = 0;
  if (existing >= 0) {
    index = static_cast<std::size_t>(existing);
  } else if (count < core_storage::kMaxSavedWifiNetworks) {
    index = count++;
  } else {
    index = count - 1;
  }
  SavedWifiNetwork replacement{};
  replacement.ssid_length = static_cast<std::uint8_t>(candidate.ssid_length);
  replacement.password_length = static_cast<std::uint8_t>(candidate.password_length);
  std::memcpy(replacement.ssid, candidate.ssid, candidate.ssid_length);
  if (candidate.password_length != 0)
    std::memcpy(replacement.password, candidate.password, candidate.password_length);
  for (std::size_t i = index; i > 0; --i)
    networks[i] = networks[i - 1];
  networks[0] = replacement;
  mbedtls_platform_zeroize(&replacement, sizeof(replacement));
  return true;
}

bool SavedWifiNetworks::forget(const char* ssid, std::size_t ssid_length) {
  const int found = find(ssid, ssid_length);
  if (found < 0)
    return false;
  const auto index = static_cast<std::size_t>(found);
  mbedtls_platform_zeroize(&networks[index], sizeof(networks[index]));
  for (std::size_t i = index; i + 1 < count; ++i)
    networks[i] = networks[i + 1];
  --count;
  mbedtls_platform_zeroize(&networks[count], sizeof(networks[count]));
  return true;
}

core_security::Status saveWifi(core_security::StorageService& storage,
                               core_security::RandomSource& random, SavedWifiNetworks& saved) {
  core_storage::WifiNetworks config{};
  config.count = saved.count;
  for (std::size_t i = 0; i < saved.count; ++i) {
    const auto profile = saved.profile(i);
    config.networks[i] = {reinterpret_cast<const std::uint8_t*>(profile.ssid), profile.ssid_length,
                          reinterpret_cast<const std::uint8_t*>(profile.password),
                          profile.password_length};
  }
  std::uint8_t plaintext[core_storage::kMaxWifiPayload]{};
  std::uint8_t encrypted[kEncryptedCapacity]{};
  std::size_t written = 0;
  auto status = core_storage::encode_wifi_networks(config, plaintext, sizeof(plaintext), written);
  if (status == core_security::Status::Ok) {
    const core_security::RecordSpec spec{kWifiPath,
                                         sizeof(kWifiPath) - 1,
                                         core_security::Namespace::Configuration,
                                         core_security::RecordType::WifiConfig,
                                         core_storage::kWifiSchema,
                                         saved.generation + 1};
    status = storage.put(spec, plaintext, written, random, encrypted, sizeof(encrypted));
    if (status == core_security::Status::Ok)
      ++saved.generation;
  }
  mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
  mbedtls_platform_zeroize(encrypted, sizeof(encrypted));
  return status;
}

core_security::Status loadWifi(core_security::StorageService& storage,
                               core_security::RandomSource& random, SavedWifiNetworks& saved) {
  saved.clear();
  std::uint8_t plaintext[core_storage::kMaxWifiPayload]{};
  std::uint8_t encrypted[kEncryptedCapacity]{};
  const core_security::RecordSpec spec{kWifiPath,
                                       sizeof(kWifiPath) - 1,
                                       core_security::Namespace::Configuration,
                                       core_security::RecordType::WifiConfig,
                                       core_storage::kWifiSchema,
                                       0};
  std::size_t size = 0;
  core_security::Metadata metadata{};
  auto result =
      storage.get(spec, encrypted, sizeof(encrypted), plaintext, sizeof(plaintext), size, metadata);
  if (result == core_security::Status::Ok &&
      metadata.schema_version == core_storage::kWifiSchema1) {
    core_storage::WifiConfig legacy{};
    result = core_storage::decode_wifi(metadata.schema_version, plaintext, size, legacy);
    if (result == core_security::Status::Ok) {
      saved.count = 1;
      saved.generation = metadata.generation;
      copyNetwork(saved.networks[0],
                  {legacy.ssid, legacy.ssid_len, legacy.password, legacy.password_len});
      result = saveWifi(storage, random, saved);
    }
  } else if (result == core_security::Status::Ok) {
    core_storage::WifiNetworks decoded{};
    result = core_storage::decode_wifi_networks(metadata.schema_version, plaintext, size, decoded);
    if (result == core_security::Status::Ok) {
      saved.count = decoded.count;
      saved.generation = metadata.generation;
      for (std::size_t i = 0; i < decoded.count; ++i)
        copyNetwork(saved.networks[i], decoded.networks[i]);
    }
  }
  mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
  mbedtls_platform_zeroize(encrypted, sizeof(encrypted));
  if (result != core_security::Status::Ok)
    saved.clear();
  return result;
}

} // namespace core_pilot
