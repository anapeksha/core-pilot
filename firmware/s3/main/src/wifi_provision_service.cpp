#include "wifi_provision_service.hpp"

#include "core_activity.hpp"
#include "core_network.hpp"
#include "core_security.hpp"
#include "mbedtls/platform_util.h"

#include <cstring>

namespace core_pilot {
namespace {
void cloneSaved(const SavedWifiNetworks& source, SavedWifiNetworks& destination) {
  destination.clear();
  destination.count = source.count;
  destination.generation = source.generation;
  std::memcpy(destination.networks, source.networks, sizeof(source.networks));
}
} // namespace

WifiProvisionService::WifiProvisionService(CoreNetwork& network,
                                           core_security::StorageService& storage,
                                           core_security::RandomSource& random,
                                           ActivityService& activity, SavedWifiNetworks& saved)
    : network_(network), storage_(storage), random_(random), activity_(activity), saved_(saved) {}

esp_err_t WifiProvisionService::start() {
  if (queue_ != nullptr)
    return ESP_ERR_INVALID_STATE;
  queue_ = xQueueCreateStatic(1, sizeof(Request), queue_storage_, &queue_buffer_);
  if (queue_ == nullptr)
    return ESP_ERR_NO_MEM;
  task_ = xTaskCreateStaticPinnedToCore(taskEntry, "wifi_provision",
                                        kTaskStackBytes / sizeof(StackType_t), this, 4, task_stack_,
                                        &task_buffer_, 0);
  return task_ == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
}

bool WifiProvisionService::enqueue(Request& request) {
  portENTER_CRITICAL(&lock_);
  const bool busy = status_.state == State::queued || status_.state == State::connecting ||
                    status_.state == State::forgetting || status_.state == State::scanning;
  if (queue_ == nullptr || busy) {
    portEXIT_CRITICAL(&lock_);
    mbedtls_platform_zeroize(&request, sizeof(request));
    return false;
  }
  const Status previous = status_;
  status_ = {State::queued, ESP_OK};
  portEXIT_CRITICAL(&lock_);

  const BaseType_t queued = xQueueSend(queue_, &request, 0);
  mbedtls_platform_zeroize(&request, sizeof(request));
  if (queued == pdTRUE)
    return true;
  portENTER_CRITICAL(&lock_);
  if (status_.state == State::queued)
    status_ = previous;
  portEXIT_CRITICAL(&lock_);
  return false;
}

esp_err_t WifiProvisionService::submit(const char* ssid, std::size_t ssid_length,
                                       const char* password, std::size_t password_length) {
  const network_policy::Profile profile{ssid, ssid_length, password, password_length};
  if (!network_policy::valid_profile(profile))
    return ESP_ERR_INVALID_ARG;
  Request request{};
  request.operation = Operation::provision;
  request.ssid_length = static_cast<std::uint8_t>(ssid_length);
  request.password_length = static_cast<std::uint8_t>(password_length);
  std::memcpy(request.ssid, ssid, ssid_length);
  if (password_length != 0)
    std::memcpy(request.password, password, password_length);
  return enqueue(request) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t WifiProvisionService::requestScan() {
  Request request{};
  request.operation = Operation::scan;
  return enqueue(request) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t WifiProvisionService::forget(const char* ssid, std::size_t ssid_length) {
  if (ssid == nullptr || ssid_length == 0 || ssid_length > network_policy::kMaxSsidBytes ||
      std::memchr(ssid, 0, ssid_length) != nullptr)
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&lock_);
  const bool exists = saved_.find(ssid, ssid_length) >= 0;
  portEXIT_CRITICAL(&lock_);
  if (!exists)
    return ESP_ERR_NOT_FOUND;
  Request request{};
  request.operation = Operation::forget;
  request.ssid_length = static_cast<std::uint8_t>(ssid_length);
  std::memcpy(request.ssid, ssid, ssid_length);
  return enqueue(request) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

WifiProvisionService::Status WifiProvisionService::status() const {
  portENTER_CRITICAL(&lock_);
  const Status snapshot = status_;
  portEXIT_CRITICAL(&lock_);
  return snapshot;
}

WifiProvisionService::SavedSnapshot WifiProvisionService::savedSnapshot() const {
  SavedSnapshot snapshot{};
  const auto network = network_.status();
  portENTER_CRITICAL(&lock_);
  snapshot.count = saved_.count;
  for (std::size_t i = 0; i < saved_.count; ++i) {
    std::memcpy(snapshot.items[i].ssid, saved_.networks[i].ssid, saved_.networks[i].ssid_length);
    snapshot.items[i].active = network.has_ipv4 && network.station_ssid[0] != '\0' &&
                               std::strcmp(network.station_ssid, saved_.networks[i].ssid) == 0;
  }
  portEXIT_CRITICAL(&lock_);
  return snapshot;
}

void WifiProvisionService::taskEntry(void* context) {
  static_cast<WifiProvisionService*>(context)->taskLoop();
}

void WifiProvisionService::taskLoop() {
  Request request{};
  while (true) {
    if (xQueueReceive(queue_, &request, portMAX_DELAY) != pdTRUE)
      continue;
    mbedtls_platform_zeroize(queue_storage_, sizeof(queue_storage_));
    if (request.operation == Operation::provision)
      provision(request);
    else if (request.operation == Operation::forget)
      forgetSaved(request);
    else
      scan();
    mbedtls_platform_zeroize(&request, sizeof(request));
  }
}

void WifiProvisionService::scan() {
  setStatus(State::scanning, ESP_OK);
  esp_err_t result = network_.requestScan();
  if (result == ESP_OK) {
    const auto generation = network_.scanSnapshot().generation;
    result = network_.waitForScan(generation, pdMS_TO_TICKS(network_policy::kScanTimeoutMs));
  }
  setStatus(result == ESP_OK ? State::committed : State::failed, result);
}

void WifiProvisionService::publishSaved(const SavedWifiNetworks& staged) {
  portENTER_CRITICAL(&lock_);
  saved_.clear();
  saved_.count = staged.count;
  saved_.generation = staged.generation;
  std::memcpy(saved_.networks, staged.networks, sizeof(staged.networks));
  portEXIT_CRITICAL(&lock_);
}

void WifiProvisionService::provision(const Request& request) {
  setStatus(State::connecting, ESP_OK);
  const auto before = network_.status();
  SavedWifiNetworks original;
  portENTER_CRITICAL(&lock_);
  cloneSaved(saved_, original);
  portEXIT_CRITICAL(&lock_);
  SavedWifiNetworks staged;
  cloneSaved(original, staged);
  const network_policy::Profile trial{request.ssid, request.ssid_length, request.password,
                                      request.password_length};
  esp_err_t result = network_.connectProfile(trial);
  bool storage_changed = false;
  if (result == ESP_OK && staged.upsert(trial)) {
    storage_changed = saveWifi(storage_, random_, staged) == core_security::Status::Ok;
    if (storage_changed)
      result = network_.commitProfile();
    else
      result = ESP_FAIL;
  } else if (result == ESP_OK) {
    result = ESP_FAIL;
  }
  if (result == ESP_OK) {
    publishSaved(staged);
    setStatus(State::committed, ESP_OK);
    activity_.append("wifi_configuration_saved", "A tested Wi-Fi network was encrypted.");
    return;
  }
  const bool storage_restored =
      !storage_changed || saveWifi(storage_, random_, original) == core_security::Status::Ok;
  esp_err_t rollback = ESP_FAIL;
  const int previous = before.station_ssid[0] == '\0'
                           ? -1
                           : original.find(before.station_ssid, std::strlen(before.station_ssid));
  if (previous >= 0) {
    rollback = network_.connectProfile(original.profile(static_cast<std::size_t>(previous)));
    if (rollback == ESP_OK)
      rollback = network_.commitProfile();
  } else {
    rollback = network_.restoreSetupAp();
  }
  const bool restored = storage_restored && rollback == ESP_OK;
  setStatus(restored ? State::rolled_back : State::failed, result);
  activity_.append(restored ? "wifi_configuration_rolled_back" : "wifi_configuration_failed",
                   restored ? "The previous network state was restored."
                            : "Wi-Fi recovery requires attention.");
}

bool WifiProvisionService::connectFirstAvailable(const SavedWifiNetworks& candidates,
                                                 const char* excluded_ssid,
                                                 std::size_t excluded_length) {
  for (std::size_t i = 0; i < candidates.count; ++i) {
    const auto profile = candidates.profile(i);
    if (profile.ssid_length == excluded_length &&
        std::memcmp(profile.ssid, excluded_ssid, excluded_length) == 0)
      continue;
    if (network_.connectProfile(profile) == ESP_OK)
      return true;
  }
  return false;
}

void WifiProvisionService::forgetSaved(const Request& request) {
  setStatus(State::forgetting, ESP_OK);
  SavedWifiNetworks original;
  portENTER_CRITICAL(&lock_);
  cloneSaved(saved_, original);
  portEXIT_CRITICAL(&lock_);
  SavedWifiNetworks staged;
  cloneSaved(original, staged);
  const auto before = network_.status();
  const bool active = before.station_ssid[0] != '\0' &&
                      std::strlen(before.station_ssid) == request.ssid_length &&
                      std::memcmp(before.station_ssid, request.ssid, request.ssid_length) == 0;
  if (!staged.forget(request.ssid, request.ssid_length)) {
    setStatus(State::failed, ESP_ERR_NOT_FOUND);
    return;
  }
  bool transitioned = true;
  if (active) {
    transitioned = connectFirstAvailable(staged, request.ssid, request.ssid_length);
    if (!transitioned)
      transitioned = network_.restoreSetupAp() == ESP_OK;
  }
  bool storage_changed = false;
  esp_err_t result = transitioned ? ESP_OK : ESP_FAIL;
  if (result == ESP_OK) {
    storage_changed = saveWifi(storage_, random_, staged) == core_security::Status::Ok;
    if (!storage_changed)
      result = ESP_FAIL;
  }
  if (result == ESP_OK && active && network_.status().has_ipv4)
    result = network_.commitProfile();
  if (result == ESP_OK) {
    publishSaved(staged);
    setStatus(State::committed, ESP_OK);
    activity_.append("wifi_network_forgotten", "A saved Wi-Fi network was removed.");
    return;
  }
  const bool storage_restored =
      !storage_changed || saveWifi(storage_, random_, original) == core_security::Status::Ok;
  esp_err_t rollback = ESP_OK;
  const int old = original.find(request.ssid, request.ssid_length);
  if (active && old >= 0) {
    rollback = network_.connectProfile(original.profile(static_cast<std::size_t>(old)));
    if (rollback == ESP_OK)
      rollback = network_.commitProfile();
  }
  const bool restored = storage_restored && rollback == ESP_OK;
  setStatus(restored ? State::rolled_back : State::failed, result);
}

void WifiProvisionService::setStatus(State state, esp_err_t error) {
  portENTER_CRITICAL(&lock_);
  status_ = {state, error};
  portEXIT_CRITICAL(&lock_);
}

} // namespace core_pilot
