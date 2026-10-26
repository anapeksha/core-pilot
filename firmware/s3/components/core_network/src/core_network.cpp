#include "core_network.hpp"

#include "esp_log.h"
#include "esp_mac.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"

#include <cstring>

namespace core_pilot {
namespace {
constexpr char kTag[] = "core_network";
constexpr EventBits_t kGotIpv4 = BIT0;
constexpr EventBits_t kDisconnected = BIT1;
constexpr std::int8_t kMaxTxPowerQuarterDbm = 40; // 10 dBm

void erase(void* ptr, std::size_t len) {
  auto* bytes = static_cast<volatile std::uint8_t*>(ptr);
  for (std::size_t index = 0; index < len; ++index) {
    bytes[index] = 0;
  }
}
} // namespace

CoreNetwork::~CoreNetwork() {
  stop();
}

void CoreNetwork::set_mode(Mode mode) {
  portENTER_CRITICAL(&lock_);
  state_ = {};
  state_.mode = mode;
  portEXIT_CRITICAL(&lock_);
}

void CoreNetwork::set_mode_preserving_station(Mode mode) {
  portENTER_CRITICAL(&lock_);
  state_.mode = mode;
  portEXIT_CRITICAL(&lock_);
}

CoreNetwork::Status CoreNetwork::status() const {
  portENTER_CRITICAL(&lock_);
  const Status snapshot = state_;
  portEXIT_CRITICAL(&lock_);
  return snapshot;
}

void CoreNetwork::on_event(void* arg, esp_event_base_t base, int32_t id, void* data) {
  auto& self = *static_cast<CoreNetwork*>(arg);
  if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
    portENTER_CRITICAL(&self.lock_);
    const bool requested = self.scan_.state == ScanState::scanning;
    portEXIT_CRITICAL(&self.lock_);
    if (!requested)
      return;
    wifi_ap_record_t records[kMaxScanResults]{};
    std::uint16_t count = kMaxScanResults;
    esp_err_t result = esp_wifi_scan_get_ap_records(&count, records);
    const Mode mode = self.status().mode;
    if (result == ESP_OK && mode == Mode::setup_ap)
      result = esp_wifi_set_mode(WIFI_MODE_AP);
    portENTER_CRITICAL(&self.lock_);
    self.scan_.last_error = result;
    self.scan_.count = result == ESP_OK ? count : 0;
    self.scan_.state = result == ESP_OK ? ScanState::complete : ScanState::failed;
    ++self.scan_.generation;
    if (self.scan_.generation == 0)
      self.scan_.generation = 1;
    if (result == ESP_OK)
      std::memcpy(self.scan_.records, records, count * sizeof(records[0]));
    portEXIT_CRITICAL(&self.lock_);
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const auto* event = static_cast<wifi_event_sta_disconnected_t*>(data);
    portENTER_CRITICAL(&self.lock_);
    self.state_.station_connected = false;
    self.state_.has_ipv4 = false;
    self.state_.has_ipv6 = false;
    self.state_.has_bssid = false;
    std::memset(self.state_.station_bssid, 0, sizeof(self.state_.station_bssid));
    const bool reconnect = self.state_.mode == Mode::station;
    portEXIT_CRITICAL(&self.lock_);
    xEventGroupSetBits(self.events_, kDisconnected);
    if (reconnect) {
      const esp_err_t result = esp_wifi_connect();
      ESP_LOGW(kTag, "Wi-Fi disconnected (reason %u), reconnect: %s",
               static_cast<unsigned>(event->reason), esp_err_to_name(result));
    }
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto* event = static_cast<ip_event_got_ip_t*>(data);
    wifi_ap_record_t access_point{};
    const bool has_access_point = esp_wifi_sta_get_ap_info(&access_point) == ESP_OK;
    portENTER_CRITICAL(&self.lock_);
    self.state_.station_connected = true;
    self.state_.has_ipv4 = true;
    self.state_.ipv4 = event->ip_info.ip;
    self.state_.has_bssid = has_access_point;
    if (has_access_point)
      std::memcpy(self.state_.station_bssid, access_point.bssid, sizeof(self.state_.station_bssid));
    portEXIT_CRITICAL(&self.lock_);
    xEventGroupSetBits(self.events_, kGotIpv4);
    // IPv6 status is optional; this creates only a link-local address.
    esp_netif_create_ip6_linklocal(self.sta_);
  } else if (base == IP_EVENT && id == IP_EVENT_GOT_IP6) {
    const auto* event = static_cast<ip_event_got_ip6_t*>(data);
    if (event->esp_netif == self.sta_) {
      portENTER_CRITICAL(&self.lock_);
      self.state_.has_ipv6 = true;
      self.state_.ipv6 = event->ip6_info.ip;
      portEXIT_CRITICAL(&self.lock_);
    }
  }
}

esp_err_t CoreNetwork::start_radio_for_boot_scan() {
  esp_err_t result = esp_wifi_set_mode(WIFI_MODE_STA);
  if (result == ESP_OK)
    result = esp_wifi_start();
  if (result == ESP_OK) {
    wifi_started_ = true;
    result = esp_wifi_set_ps(WIFI_PS_NONE);
  }
  if (result == ESP_OK)
    result = esp_wifi_set_max_tx_power(kMaxTxPowerQuarterDbm);
  return result;
}

bool CoreNetwork::profile_visible(const network_policy::Profile& profile,
                                  const wifi_ap_record_t* records, std::size_t count) const {
  for (std::size_t i = 0; i < count; ++i) {
    const auto length =
        strnlen(reinterpret_cast<const char*>(records[i].ssid), sizeof(records[i].ssid));
    if (length == profile.ssid_length &&
        std::memcmp(records[i].ssid, profile.ssid, profile.ssid_length) == 0)
      return true;
  }
  return false;
}

esp_err_t CoreNetwork::start_station(const network_policy::Profile& profile) {
  wifi_config_t config{};
  std::memcpy(config.sta.ssid, profile.ssid, profile.ssid_length);
  if (profile.password_length != 0) {
    std::memcpy(config.sta.password, profile.password, profile.password_length);
  }
  config.sta.threshold.authmode =
      profile.password_length == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  config.sta.pmf_cfg.capable = true;
  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err == ESP_OK)
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
  erase(&config, sizeof config);
  if (err != ESP_OK) {
    return err;
  }
  set_mode(Mode::station);
  portENTER_CRITICAL(&lock_);
  std::memcpy(state_.station_ssid, profile.ssid, profile.ssid_length);
  state_.station_ssid[profile.ssid_length] = '\0';
  portEXIT_CRITICAL(&lock_);
  if (!wifi_started_) {
    err = esp_wifi_start();
    if (err != ESP_OK)
      return err;
    wifi_started_ = true;
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK)
      return err;
    err = esp_wifi_set_max_tx_power(kMaxTxPowerQuarterDbm);
    if (err != ESP_OK)
      return err;
  }
  xEventGroupClearBits(events_, kGotIpv4 | kDisconnected);
  err = esp_wifi_connect();
  if (err != ESP_OK) {
    return err;
  }
  const EventBits_t bits = xEventGroupWaitBits(events_, kGotIpv4 | kDisconnected, pdTRUE, pdFALSE,
                                               pdMS_TO_TICKS(network_policy::kJoinTimeoutMs));
  return (bits & kGotIpv4) != 0 && status().has_ipv4 ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t CoreNetwork::start_setup_ap() {
  if (wifi_started_) {
    const esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
      return err;
    }
    wifi_started_ = false;
  }
  // The old station profile is no longer needed; remove it from driver RAM.
  esp_err_t err = ESP_OK;
  if (status().mode == Mode::station) {
    wifi_config_t cleared{};
    err = esp_wifi_set_config(WIFI_IF_STA, &cleared);
    if (err != ESP_OK) {
      return err;
    }
  }
  std::uint8_t mac[6]{};
  err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  if (err != ESP_OK) {
    return err;
  }
  wifi_config_t config{};
  char name[sizeof(network_policy::kSetupPrefix) + 6]{};
  if (!network_policy::setup_ssid(mac, name, sizeof name)) {
    return ESP_ERR_INVALID_ARG;
  }
  std::memcpy(config.ap.ssid, name, std::strlen(name));
  config.ap.ssid_len = static_cast<std::uint8_t>(std::strlen(name));
  config.ap.channel = 1;
  config.ap.max_connection = 4;
  config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  std::memcpy(config.ap.password, setup_password_, sizeof(setup_password_));
  // DHCP is provided by esp_netif's default AP. Keep the subnet fixed.
  err = esp_netif_dhcps_stop(ap_);
  if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
    return err;
  }
  esp_netif_ip_info_t address{};
  IP4_ADDR(&address.ip, 192, 168, 4, 1);
  IP4_ADDR(&address.gw, 192, 168, 4, 1);
  IP4_ADDR(&address.netmask, 255, 255, 255, 0);
  err = esp_netif_set_ip_info(ap_, &address);
  if (err == ESP_OK) {
    err = esp_netif_dhcps_start(ap_);
  }
  if (err == ESP_OK) {
    err = esp_wifi_set_mode(WIFI_MODE_AP);
  }
  if (err == ESP_OK) {
    err = esp_wifi_set_config(WIFI_IF_AP, &config);
  }
  std::memset(&config, 0, sizeof(config));
  if (err == ESP_OK) {
    set_mode(Mode::setup_ap);
    portENTER_CRITICAL(&lock_);
    std::memcpy(state_.setup_ssid, name, sizeof(name));
    portEXIT_CRITICAL(&lock_);
    err = esp_wifi_start();
    if (err == ESP_OK) {
      wifi_started_ = true;
      err = esp_wifi_set_max_tx_power(kMaxTxPowerQuarterDbm);
    }
  }
  return err;
}

esp_err_t CoreNetwork::start_mdns() {
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    return err;
  }
  mdns_started_ = true;
  err = mdns_hostname_set(network_policy::kHostname);
  if (err == ESP_OK) {
    err = mdns_instance_name_set("Core Pilot");
  }
  return err;
}

esp_err_t CoreNetwork::begin(const network_policy::Profile* profiles, std::size_t profile_count,
                             const char* setup_password, ProgressCallback progress,
                             void* progress_context) {
  if (events_ != nullptr || setup_password == nullptr || std::strlen(setup_password) != 16 ||
      profile_count > network_policy::kMaxSavedNetworks ||
      (profile_count != 0 && profiles == nullptr)) {
    return ESP_ERR_INVALID_STATE;
  }
  std::memcpy(setup_password_, setup_password, sizeof(setup_password_));
  events_ = xEventGroupCreate();
  if (events_ == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  // ESP-IDF's netif initialization is process-wide; do not deinitialize it
  // because other components may also own interfaces.
  esp_err_t err = esp_netif_init();
  if (err == ESP_OK) {
    err = esp_event_loop_create_default();
    if (err == ESP_OK) {
      loop_owned_ = true;
    } else if (err == ESP_ERR_INVALID_STATE) {
      err = ESP_OK; // another component owns the default event loop
    }
  }
  if (err == ESP_OK) {
    sta_ = esp_netif_create_default_wifi_sta();
    ap_ = esp_netif_create_default_wifi_ap();
    if (sta_ == nullptr || ap_ == nullptr) {
      err = ESP_ERR_NO_MEM;
    }
  }
  if (err == ESP_OK) {
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    wifi_initialized_ = err == ESP_OK;
  }
  if (err == ESP_OK) {
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  }
  if (err == ESP_OK) {
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, this,
                                              &wifi_handler_);
  }
  if (err == ESP_OK) {
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, this,
                                              &ip4_handler_);
  }
  if (err == ESP_OK) {
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_GOT_IP6, on_event, this,
                                              &ip6_handler_);
  }
  if (err == ESP_OK && profile_count != 0) {
    if (progress != nullptr)
      progress(progress_context, "Scanning for saved networks");
    err = start_radio_for_boot_scan();
    wifi_ap_record_t records[kMaxScanResults]{};
    std::uint16_t record_count = kMaxScanResults;
    if (err == ESP_OK)
      err = esp_wifi_scan_start(nullptr, true);
    if (err == ESP_OK)
      err = esp_wifi_scan_get_ap_records(&record_count, records);
    bool connected = false;
    const std::size_t candidate_limit =
        network_policy::kBootJoinBudgetMs / network_policy::kJoinTimeoutMs;
    std::size_t attempted = 0;
    if (err == ESP_OK) {
      for (std::size_t i = 0; i < profile_count && attempted < candidate_limit; ++i) {
        if (!network_policy::valid_profile(profiles[i]) ||
            !profile_visible(profiles[i], records, record_count))
          continue;
        if (progress != nullptr)
          progress(progress_context, "Trying a saved network");
        ++attempted;
        if (start_station(profiles[i]) == ESP_OK) {
          connected = true;
          break;
        }
      }
      err = connected ? ESP_OK : ESP_ERR_TIMEOUT;
    }
    if (!connected) {
      if (progress != nullptr)
        progress(progress_context, "Starting setup access point");
      err = start_setup_ap();
    }
  } else if (err == ESP_OK) {
    if (progress != nullptr)
      progress(progress_context, "Starting setup access point");
    err = start_setup_ap();
  }
  if (err == ESP_OK) {
    err = start_mdns();
  }
  if (err != ESP_OK) {
    stop();
  }
  return err;
}

esp_err_t CoreNetwork::requestScan() {
  const Mode mode = status().mode;
  if (mode != Mode::station && mode != Mode::setup_ap)
    return ESP_ERR_INVALID_STATE;
  if (mode == Mode::setup_ap) {
    const esp_err_t mode_result = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (mode_result != ESP_OK)
      return mode_result;
  }
  portENTER_CRITICAL(&lock_);
  if (scan_.state == ScanState::scanning) {
    portEXIT_CRITICAL(&lock_);
    return ESP_ERR_INVALID_STATE;
  }
  scan_.state = ScanState::scanning;
  scan_.last_error = ESP_OK;
  scan_.count = 0;
  portEXIT_CRITICAL(&lock_);

  esp_err_t result = esp_wifi_scan_start(nullptr, false);
  if (result != ESP_OK) {
    if (mode == Mode::setup_ap) {
      const esp_err_t restore_result = esp_wifi_set_mode(WIFI_MODE_AP);
      if (restore_result != ESP_OK)
        result = restore_result;
    }
    portENTER_CRITICAL(&lock_);
    scan_.state = ScanState::failed;
    scan_.last_error = result;
    portEXIT_CRITICAL(&lock_);
  }
  return result;
}

CoreNetwork::ScanSnapshot CoreNetwork::scanSnapshot() const {
  portENTER_CRITICAL(&lock_);
  const ScanSnapshot snapshot = scan_;
  portEXIT_CRITICAL(&lock_);
  return snapshot;
}

esp_err_t CoreNetwork::connectProfile(const network_policy::Profile& profile) {
  if (!network_policy::valid_profile(profile) || !wifi_started_)
    return ESP_ERR_INVALID_STATE;
  const Mode initial_mode = status().mode;
  if (initial_mode != Mode::station && initial_mode != Mode::setup_ap)
    return ESP_ERR_INVALID_STATE;
  esp_wifi_scan_stop();
  esp_wifi_disconnect();
  if (initial_mode == Mode::setup_ap) {
    const esp_err_t mode_result = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (mode_result != ESP_OK)
      return mode_result;
  }
  wifi_config_t config{};
  std::memcpy(config.sta.ssid, profile.ssid, profile.ssid_length);
  if (profile.password_length != 0)
    std::memcpy(config.sta.password, profile.password, profile.password_length);
  config.sta.threshold.authmode =
      profile.password_length == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  config.sta.pmf_cfg.capable = true;
  esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &config);
  erase(&config, sizeof(config));
  if (result != ESP_OK)
    return result;

  portENTER_CRITICAL(&lock_);
  state_.station_connected = false;
  state_.has_ipv4 = false;
  state_.has_bssid = false;
  std::memcpy(state_.station_ssid, profile.ssid, profile.ssid_length);
  state_.station_ssid[profile.ssid_length] = '\0';
  portEXIT_CRITICAL(&lock_);
  xEventGroupClearBits(events_, kGotIpv4 | kDisconnected);
  result = esp_wifi_connect();
  if (result != ESP_OK)
    return result;
  const EventBits_t bits = xEventGroupWaitBits(events_, kGotIpv4 | kDisconnected, pdTRUE, pdFALSE,
                                               pdMS_TO_TICKS(network_policy::kJoinTimeoutMs));
  if ((bits & kGotIpv4) != 0 && status().has_ipv4)
    return ESP_OK;
  esp_wifi_disconnect();
  if (initial_mode == Mode::setup_ap) {
    esp_wifi_disconnect();
    wifi_config_t cleared{};
    esp_wifi_set_config(WIFI_IF_STA, &cleared);
    portENTER_CRITICAL(&lock_);
    state_.station_connected = false;
    state_.has_ipv4 = false;
    state_.has_ipv6 = false;
    state_.has_bssid = false;
    state_.station_ssid[0] = '\0';
    portEXIT_CRITICAL(&lock_);
    const esp_err_t mode_result = esp_wifi_set_mode(WIFI_MODE_AP);
    if (mode_result != ESP_OK)
      return mode_result;
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t CoreNetwork::commitProfile() {
  if (!wifi_started_)
    return ESP_ERR_INVALID_STATE;
  const Status snapshot = status();
  if (!snapshot.station_connected || !snapshot.has_ipv4)
    return ESP_ERR_INVALID_STATE;
  if (snapshot.mode == Mode::station)
    return ESP_OK;
  if (snapshot.mode != Mode::setup_ap)
    return ESP_ERR_INVALID_STATE;
  const esp_err_t result = esp_wifi_set_mode(WIFI_MODE_STA);
  if (result == ESP_OK) {
    set_mode_preserving_station(Mode::station);
    std::memset(setup_password_, 0, sizeof(setup_password_));
  }
  return result;
}

esp_err_t CoreNetwork::restoreSetupAp() {
  if (!wifi_started_)
    return ESP_ERR_INVALID_STATE;
  esp_wifi_scan_stop();
  esp_wifi_disconnect();
  wifi_config_t cleared{};
  esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &cleared);
  if (result == ESP_OK)
    result = esp_wifi_set_mode(WIFI_MODE_AP);
  if (result == ESP_OK) {
    portENTER_CRITICAL(&lock_);
    state_.mode = Mode::setup_ap;
    state_.station_connected = false;
    state_.has_ipv4 = false;
    state_.ipv4 = {};
    state_.station_ssid[0] = '\0';
    state_.has_bssid = false;
    std::memset(state_.station_bssid, 0, sizeof(state_.station_bssid));
    state_.has_ipv6 = false;
    state_.ipv6 = {};
    portEXIT_CRITICAL(&lock_);
  }
  return result;
}

void CoreNetwork::stop() {
  if (mdns_started_) {
    mdns_free();
    mdns_started_ = false;
  }
  if (ip6_handler_ != nullptr) {
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_GOT_IP6, ip6_handler_);
    ip6_handler_ = nullptr;
  }
  if (ip4_handler_ != nullptr) {
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip4_handler_);
    ip4_handler_ = nullptr;
  }
  if (wifi_handler_ != nullptr) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_);
    wifi_handler_ = nullptr;
  }
  if (wifi_started_) {
    esp_wifi_stop();
    wifi_started_ = false;
  }
  if (wifi_initialized_) {
    esp_wifi_deinit();
    wifi_initialized_ = false;
  }
  if (ap_ != nullptr) {
    esp_netif_destroy_default_wifi(ap_);
    ap_ = nullptr;
  }
  if (sta_ != nullptr) {
    esp_netif_destroy_default_wifi(sta_);
    sta_ = nullptr;
  }
  if (loop_owned_) {
    esp_event_loop_delete_default();
    loop_owned_ = false;
  }
  if (events_ != nullptr) {
    vEventGroupDelete(events_);
    events_ = nullptr;
  }
  std::memset(setup_password_, 0, sizeof(setup_password_));
  set_mode(Mode::stopped);
}

} // namespace core_pilot
