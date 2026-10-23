#pragma once

#include "core_network_policy.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

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
  enum class State : std::uint8_t { idle, queued, connecting, committed, rolled_back, failed };
  struct Status final {
    State state{State::idle};
    esp_err_t last_error{ESP_OK};
  };

  WifiProvisionService(CoreNetwork& network, core_security::StorageService& storage,
                       core_security::RandomSource& random, ActivityService& activity);
  ~WifiProvisionService();
  esp_err_t start(const network_policy::Profile* current);
  esp_err_t submit(const char* ssid, std::size_t ssid_length, const char* password,
                   std::size_t password_length);
  Status status() const;

private:
  static constexpr std::uint32_t kTaskStackBytes = 4096;

  struct Credentials final {
    std::uint8_t ssid_length{};
    std::uint8_t password_length{};
    char ssid[network_policy::kMaxSsidBytes + 1]{};
    char password[network_policy::kMaxPasswordBytes + 1]{};
  };
  static_assert(std::is_trivially_copyable_v<Credentials>);

  static void taskEntry(void* context);
  void taskLoop();
  void setStatus(State state, esp_err_t error);

  CoreNetwork& network_;
  core_security::StorageService& storage_;
  core_security::RandomSource& random_;
  ActivityService& activity_;
  Credentials current_{};
  bool has_current_{};
  StaticQueue_t queue_buffer_{};
  std::uint8_t queue_storage_[sizeof(Credentials)]{};
  QueueHandle_t queue_{nullptr};
  StaticTask_t task_buffer_{};
  static_assert(kTaskStackBytes % sizeof(StackType_t) == 0);
  StackType_t task_stack_[kTaskStackBytes / sizeof(StackType_t)]{};
  TaskHandle_t task_{nullptr};
  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  Status status_{};
};

} // namespace core_pilot
