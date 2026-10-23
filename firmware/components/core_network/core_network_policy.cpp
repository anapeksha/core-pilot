#include "core_network_policy.hpp"

#include <cstring>

namespace core_pilot::network_policy {

bool valid_profile(const Profile& profile) {
  if (profile.ssid == nullptr || profile.ssid_length == 0 || profile.ssid_length > kMaxSsidBytes ||
      (profile.password_length != 0 &&
       (profile.password == nullptr || profile.password_length < 8 ||
        profile.password_length > kMaxPasswordBytes))) {
    return false;
  }
  return std::memchr(profile.ssid, 0, profile.ssid_length) == nullptr &&
         (profile.password_length == 0 ||
          std::memchr(profile.password, 0, profile.password_length) == nullptr);
}

std::size_t scan_limit(std::size_t requested) {
  return requested < kMaxScanResults ? requested : kMaxScanResults;
}

bool setup_ssid(const std::uint8_t mac[6], char* out, std::size_t capacity) {
  if (mac == nullptr || out == nullptr || capacity < sizeof(kSetupPrefix) + 6) {
    return false;
  }
  std::memcpy(out, kSetupPrefix, sizeof(kSetupPrefix) - 1);
  constexpr char hex[] = "0123456789ABCDEF";
  const std::size_t offset = sizeof(kSetupPrefix) - 1;
  for (std::size_t i = 0; i < 3; ++i) {
    out[offset + 2 * i] = hex[mac[i + 3] >> 4];
    out[offset + 2 * i + 1] = hex[mac[i + 3] & 15];
  }
  out[offset + 6] = '\0';
  return true;
}

} // namespace core_pilot::network_policy
