#include "wifi_profile.hpp"

#include "mbedtls/platform_util.h"
#include "wifi_codec.hpp"

#include <cstddef>

namespace core_pilot {
namespace {
constexpr char kWifiPath[] = "/core-pilot/config/wifi.enc";
} // namespace

LoadedWifi::~LoadedWifi() {
  clear();
}

void LoadedWifi::clear() {
  mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
  mbedtls_platform_zeroize(encrypted, sizeof(encrypted));
  profile = {};
}

core_security::Status saveWifi(core_security::StorageService& storage,
                               core_security::RandomSource& random, const char* ssid,
                               std::size_t ssid_length, const char* password,
                               std::size_t password_length) {
  if (ssid == nullptr || password == nullptr || ssid_length == 0 ||
      ssid_length > network_policy::kMaxSsidBytes ||
      password_length > network_policy::kMaxPasswordBytes) {
    return core_security::Status::Invalid;
  }
  static constexpr std::uint8_t kHostname[] = {'c', 'o', 'r', 'e', 'p', 'i', 'l', 'o', 't'};
  const core_storage::WifiConfig config{reinterpret_cast<const std::uint8_t*>(ssid),
                                        ssid_length,
                                        reinterpret_cast<const std::uint8_t*>(password),
                                        password_length,
                                        kHostname,
                                        sizeof(kHostname)};
  std::uint8_t plaintext[core_storage::kMaxWifiPayload]{};
  std::uint8_t encrypted[256]{};
  std::size_t written = 0;
  auto status = core_storage::encode_wifi(config, plaintext, sizeof(plaintext), written);
  if (status == core_security::Status::Ok) {
    const core_security::RecordSpec spec{kWifiPath,
                                         sizeof(kWifiPath) - 1,
                                         core_security::Namespace::Configuration,
                                         core_security::RecordType::WifiConfig,
                                         core_storage::kWifiSchema,
                                         1};
    status = storage.put(spec, plaintext, written, random, encrypted, sizeof(encrypted));
  }
  mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
  mbedtls_platform_zeroize(encrypted, sizeof(encrypted));
  return status;
}

core_security::Status loadWifi(core_security::StorageService& storage, LoadedWifi& loaded) {
  const core_security::RecordSpec spec{kWifiPath,
                                       sizeof(kWifiPath) - 1,
                                       core_security::Namespace::Configuration,
                                       core_security::RecordType::WifiConfig,
                                       core_storage::kWifiSchema,
                                       0};
  std::size_t size = 0;
  core_security::Metadata metadata{};
  auto result = storage.get(spec, loaded.encrypted, sizeof(loaded.encrypted), loaded.plaintext,
                            sizeof(loaded.plaintext), size, metadata);
  if (result != core_security::Status::Ok) {
    return result;
  }

  core_storage::WifiConfig config{};
  result = core_storage::decode_wifi(metadata.schema_version, loaded.plaintext, size, config);
  if (result != core_security::Status::Ok) {
    return result;
  }
  loaded.profile = {reinterpret_cast<const char*>(config.ssid), config.ssid_len,
                    reinterpret_cast<const char*>(config.password), config.password_len};
  return core_security::Status::Ok;
}

} // namespace core_pilot
