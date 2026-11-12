#pragma once

#include <cstddef>
#include <cstdint>

namespace core_pilot::network_policy {

constexpr std::size_t kMaxSsidBytes = 32;
constexpr std::size_t kMaxPasswordBytes = 63;
constexpr std::size_t kMaxScanResults = 32;
constexpr std::size_t kMaxSavedNetworks = 8;
constexpr unsigned kJoinTimeoutMs = 8000;
constexpr unsigned kScanTimeoutMs = 10000;
constexpr unsigned kBootJoinBudgetMs = 24000;
constexpr char kHostname[] = "corepilot";
constexpr char kSetupPrefix[] = "CorePilot-Setup-";

enum class ScanEventAction : std::uint8_t { ignore, finish_cancellation, publish };

// A timed-out scan remains cancellation-pending until its eventual driver event
// is consumed. This prevents that stale event from completing a newer scan.
constexpr ScanEventAction scan_event_action(bool scanning, bool cancellation_pending) {
  if (cancellation_pending)
    return ScanEventAction::finish_cancellation;
  return scanning ? ScanEventAction::publish : ScanEventAction::ignore;
}

constexpr bool scan_result_is_current(bool scanning, bool cancellation_pending,
                                      std::uint32_t current_generation,
                                      std::uint32_t event_generation) {
  return scanning && !cancellation_pending && current_generation == event_generation;
}

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
bool association_matches(const char* expected_ssid, const std::uint8_t* expected_bssid,
                         bool require_bssid, const std::uint8_t* actual_ssid,
                         std::size_t actual_ssid_length, const std::uint8_t actual_bssid[6]);
std::size_t scan_limit(std::size_t requested);
// out must have room for sizeof(kSetupPrefix) + 6 bytes (including NUL).
bool setup_ssid(const std::uint8_t mac[6], char* out, std::size_t capacity);

} // namespace core_pilot::network_policy
