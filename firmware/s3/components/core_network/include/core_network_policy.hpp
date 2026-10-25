#pragma once

#include <cstddef>
#include <cstdint>

namespace core_pilot::network_policy {

constexpr std::size_t kMaxSsidBytes = 32;
constexpr std::size_t kMaxPasswordBytes = 63;
constexpr std::size_t kMaxScanResults = 32;
constexpr unsigned kMaxJoinAttempts = 3;
constexpr unsigned kJoinTimeoutMs = 8000;
constexpr char kHostname[] = "corepilot";
constexpr char kSetupPrefix[] = "CorePilot-Setup-";

struct Profile {
  const char* ssid = nullptr;
  std::size_t ssid_length = 0;
  const char* password = nullptr;
  std::size_t password_length = 0;
};

// Lengths are explicit: input need not be NUL-terminated. Password is either
// empty (open network) or a WPA passphrase; no profile is retained by the
// policy.
bool valid_profile(const Profile& profile);
std::size_t scan_limit(std::size_t requested);
// out must have room for sizeof(kSetupPrefix) + 6 bytes (including NUL).
bool setup_ssid(const std::uint8_t mac[6], char* out, std::size_t capacity);

} // namespace core_pilot::network_policy
