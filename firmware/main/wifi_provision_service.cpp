#include "wifi_provision_service.hpp"

#include "core_activity.hpp"
#include "core_network.hpp"
#include "core_security.hpp"
#include "mbedtls/platform_util.h"
#include "wifi_profile.hpp"

#include <cstring>

namespace core_pilot {

WifiProvisionService::WifiProvisionService(CoreNetwork& network,
                                           core_security::StorageService& storage,
                                           core_security::RandomSource& random,
                                           ActivityService& activity)
    : network_(network), storage_(storage), random_(random), activity_(activity) {}

WifiProvisionService::~WifiProvisionService() {
  mbedtls_platform_zeroize(&current_, sizeof(current_));
}

esp_err_t WifiProvisionService::start(const network_policy::Profile* current) {
  if (queue_ != nullptr)
    return ESP_ERR_INVALID_STATE;
  if (current != nullptr) {
    if (!network_policy::valid_profile(*current))
      return ESP_ERR_INVALID_ARG;
    current_.ssid_length = static_cast<std::uint8_t>(current->ssid_length);
    current_.password_length = static_cast<std::uint8_t>(current->password_length);
    std::memcpy(current_.ssid, current->ssid, current->ssid_length);
    if (current->password_length != 0)
      std::memcpy(current_.password, current->password, current->password_length);
    has_current_ = true;
  }
  queue_ = xQueueCreateStatic(1, sizeof(Credentials), queue_storage_, &queue_buffer_);
  if (queue_ == nullptr)
    return ESP_ERR_NO_MEM;
  task_ = xTaskCreateStaticPinnedToCore(taskEntry, "wifi_provision",
                                        kTaskStackBytes / sizeof(StackType_t), this, 4, task_stack_,
                                        &task_buffer_, 0);
  return task_ == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t WifiProvisionService::submit(const char* ssid, std::size_t ssid_length,
                                       const char* password, std::size_t password_length) {
  const network_policy::Profile profile{ssid, ssid_length, password, password_length};
  if (queue_ == nullptr || !network_policy::valid_profile(profile))
    return ESP_ERR_INVALID_ARG;
  const auto snapshot = status();
  if (snapshot.state == State::queued || snapshot.state == State::connecting)
    return ESP_ERR_INVALID_STATE;
  Credentials credentials{};
  credentials.ssid_length = static_cast<std::uint8_t>(ssid_length);
  credentials.password_length = static_cast<std::uint8_t>(password_length);
  std::memcpy(credentials.ssid, ssid, ssid_length);
  if (password_length != 0)
    std::memcpy(credentials.password, password, password_length);
  const BaseType_t queued = xQueueSend(queue_, &credentials, 0);
  mbedtls_platform_zeroize(&credentials, sizeof(credentials));
  if (queued != pdTRUE)
    return ESP_ERR_NO_MEM;
  setStatus(State::queued, ESP_OK);
  return ESP_OK;
}

WifiProvisionService::Status WifiProvisionService::status() const {
  portENTER_CRITICAL(&lock_);
  const Status snapshot = status_;
  portEXIT_CRITICAL(&lock_);
  return snapshot;
}

void WifiProvisionService::taskEntry(void* context) {
  static_cast<WifiProvisionService*>(context)->taskLoop();
}

void WifiProvisionService::taskLoop() {
  Credentials candidate{};
  while (true) {
    if (xQueueReceive(queue_, &candidate, portMAX_DELAY) != pdTRUE)
      continue;
    setStatus(State::connecting, ESP_OK);
    const network_policy::Profile trial{candidate.ssid, candidate.ssid_length, candidate.password,
                                        candidate.password_length};
    esp_err_t result = network_.connectProfile(trial);
    if (result == ESP_OK) {
      const auto saved = saveWifi(storage_, random_, candidate.ssid, candidate.ssid_length,
                                  candidate.password, candidate.password_length);
      if (saved == core_security::Status::Ok) {
        mbedtls_platform_zeroize(&current_, sizeof(current_));
        current_ = candidate;
        has_current_ = true;
        const esp_err_t commit_result = network_.commitProfile();
        setStatus(State::committed, commit_result);
        activity_.append("wifi_configuration_saved", "A tested Wi-Fi profile was encrypted.");
        result = ESP_OK;
      } else {
        result = ESP_FAIL;
      }
    }
    if (result != ESP_OK) {
      esp_err_t rollback_result = ESP_FAIL;
      if (has_current_) {
        const network_policy::Profile rollback{current_.ssid, current_.ssid_length,
                                               current_.password, current_.password_length};
        rollback_result = network_.connectProfile(rollback);
      } else {
        rollback_result = network_.restoreSetupAp();
      }
      setStatus(rollback_result == ESP_OK ? State::rolled_back : State::failed, result);
      activity_.append(rollback_result == ESP_OK ? "wifi_configuration_rolled_back"
                                                 : "wifi_configuration_failed",
                       rollback_result == ESP_OK
                           ? (has_current_ ? "The previous Wi-Fi profile was restored."
                                           : "Setup mode remains available for another attempt.")
                           : "Wi-Fi recovery requires attention.");
    }
    mbedtls_platform_zeroize(&candidate, sizeof(candidate));
  }
}

void WifiProvisionService::setStatus(State state, esp_err_t error) {
  portENTER_CRITICAL(&lock_);
  status_ = {state, error};
  portEXIT_CRITICAL(&lock_);
}

} // namespace core_pilot
