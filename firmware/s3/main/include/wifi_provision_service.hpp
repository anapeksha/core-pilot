#pragma once

#include "core_network_policy.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "wifi_profile.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace core_pilot {
class ActivityService;
class CoreNetwork;
} // namespace core_pilot

namespace core_security {
class RandomSource;
class StorageService;
} // namespace core_security

namespace core_pilot {

class WifiProvisionService final {
public:
  enum class State : std::uint8_t {
    idle,
    queued,
    connecting,
    forgetting,
    committed,
    rolled_back,
    failed
  };
  struct Status final {
    State state{State::idle};
    esp_err_t last_error{ESP_OK};
  };
  struct SavedItem final {
    char ssid[network_policy::kMaxSsidBytes + 1]{};
    bool active{};
  };
  struct SavedSnapshot final {
    SavedItem items[core_storage::kMaxSavedWifiNetworks]{};
    std::size_t count{};
  };

  WifiProvisionService(CoreNetwork& network, core_security::StorageService& storage,
                       core_security::RandomSource& random, ActivityService& activity,
                       SavedWifiNetworks& saved);
  ~WifiProvisionService() = default;
  esp_err_t start();
  esp_err_t submit(const char* ssid, std::size_t ssid_length, const char* password,
                   std::size_t password_length);
  esp_err_t forget(const char* ssid, std::size_t ssid_length);
  Status status() const;
  SavedSnapshot savedSnapshot() const;

private:
  static constexpr std::uint32_t kTaskStackBytes = 6144;
  enum class Operation : std::uint8_t { provision, forget };
  struct Request final {
    Operation operation{Operation::provision};
    std::uint8_t ssid_length{};
    std::uint8_t password_length{};
    char ssid[network_policy::kMaxSsidBytes + 1]{};
    char password[network_policy::kMaxPasswordBytes + 1]{};
  };
  static_assert(std::is_trivially_copyable_v<Request>);

  static void taskEntry(void* context);
  void taskLoop();
  void provision(const Request& request);
  void forgetSaved(const Request& request);
  bool enqueue(Request& request);
  bool connectFirstAvailable(const SavedWifiNetworks& candidates, const char* excluded_ssid,
                             std::size_t excluded_length);
  void publishSaved(const SavedWifiNetworks& staged);
  void setStatus(State state, esp_err_t error);

  CoreNetwork& network_;
  core_security::StorageService& storage_;
  core_security::RandomSource& random_;
  ActivityService& activity_;
  SavedWifiNetworks& saved_;
  StaticQueue_t queue_buffer_{};
  std::uint8_t queue_storage_[sizeof(Request)]{};
  QueueHandle_t queue_{nullptr};
  StaticTask_t task_buffer_{};
  static_assert(kTaskStackBytes % sizeof(StackType_t) == 0);
  StackType_t task_stack_[kTaskStackBytes / sizeof(StackType_t)]{};
  TaskHandle_t task_{nullptr};
  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  Status status_{};
};

} // namespace core_pilot
