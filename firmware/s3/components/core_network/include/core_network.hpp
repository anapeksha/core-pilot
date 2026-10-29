#pragma once

#include "core_network_policy.hpp"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <cstddef>
#include <cstdint>

namespace core_pilot {

// Single-owner, task-context API. Event callbacks only update bounded state;
// callers must not invoke begin/stop/scan concurrently or from an event
// callback.
class CoreNetwork {
public:
  enum class Mode : std::uint8_t { stopped, station, setup_ap };
  using ProgressCallback = void (*)(void* context, const char* message);
  struct Status {
    Mode mode = Mode::stopped;
    bool station_connected = false;
    bool has_ipv4 = false;
    esp_ip4_addr_t ipv4{};
    char station_ssid[network_policy::kMaxSsidBytes + 1]{};
    char setup_ssid[sizeof(network_policy::kSetupPrefix) + 6]{};
    bool has_bssid = false;
    std::uint8_t station_bssid[6]{};
    bool has_ipv6 = false;
    esp_ip6_addr_t ipv6{};
  };

  enum class ScanState : std::uint8_t { idle, scanning, complete, failed };
  static constexpr std::size_t kMaxScanResults = 32;
  struct ScanSnapshot final {
    ScanState state{ScanState::idle};
    esp_err_t last_error{ESP_OK};
    std::uint32_t generation{};
    std::size_t count{};
    wifi_ap_record_t records[kMaxScanResults]{};
  };

  CoreNetwork() = default;
  ~CoreNetwork();
  CoreNetwork(const CoreNetwork&) = delete;
  CoreNetwork& operator=(const CoreNetwork&) = delete;

  // Supplies credentials in caller-owned memory. Never persists or logs them.
  // Invalid/missing profiles enter setup AP. Exhausted bounded join attempts
  // also enter setup AP. Returns an ESP-IDF error if even setup cannot start.
  esp_err_t begin(const network_policy::Profile* profiles, std::size_t profile_count,
                  const char* setup_password, ProgressCallback progress = nullptr,
                  void* progress_context = nullptr);
  void stop();
  Status status() const;

  // Starts one nonblocking station scan. WIFI_EVENT_SCAN_DONE publishes a
  // bounded immutable snapshot; no HTTP callback waits on the radio.
  esp_err_t requestScan();
  ScanSnapshot scanSnapshot() const;

  // Core-0 network owner only. Replaces the in-RAM station profile and waits
  // for bounded DHCP completion; it never persists credentials. When called
  // from setup mode, the AP remains reachable until commitProfile() succeeds.
  esp_err_t connectProfile(const network_policy::Profile& profile);
  esp_err_t commitProfile();
  esp_err_t restoreSetupAp();

private:
  static void on_event(void* arg, esp_event_base_t base, int32_t id, void* data);
  esp_err_t start_station(const network_policy::Profile& profile);
  esp_err_t start_radio_for_boot_scan();
  bool profile_visible(const network_policy::Profile& profile, const wifi_ap_record_t* records,
                       std::size_t count) const;
  esp_err_t start_setup_ap();
  esp_err_t start_mdns();
  void set_mode(Mode mode);
  void set_mode_preserving_station(Mode mode);

  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  Status state_{};
  ScanSnapshot scan_{};
  EventGroupHandle_t events_ = nullptr;
  esp_netif_t* sta_ = nullptr;
  esp_netif_t* ap_ = nullptr;
  esp_event_handler_instance_t wifi_handler_ = nullptr;
  esp_event_handler_instance_t ip4_handler_ = nullptr;
  esp_event_handler_instance_t ip6_handler_ = nullptr;

  bool loop_owned_ = false;
  bool wifi_initialized_ = false;
  bool wifi_started_ = false;
  bool mdns_started_ = false;
  bool reconnect_enabled_ = false;
  bool transition_in_progress_ = false;
  char setup_password_[17]{};
};

} // namespace core_pilot
