#pragma once

#include "core_domain.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <cstddef>
#include <cstdint>

namespace core_pilot {

class ActivityService;
class CoreMatter;
class DeviceRegistry;
class ModelRepository;

class AutomationService final {
public:
  enum class SubmitResult : std::uint8_t {
    queued,
    not_found,
    disabled,
    not_manual,
    unsupported_action,
    target_unavailable,
    queue_full,
  };

  AutomationService(ModelRepository& models, DeviceRegistry& devices, CoreMatter& matter,
                    ActivityService& activity);
  esp_err_t start();
  SubmitResult submit(const char* id);

private:
  static constexpr std::size_t kQueueDepth = 4;
  static constexpr std::size_t kTaskStackBytes = 8192;
  static constexpr TickType_t kMatterCommandTimeout = pdMS_TO_TICKS(30000);

  struct Work final {
    core_domain::ModelUpsert automation{};
  };

  static void taskEntry(void* context);
  void taskLoop();
  void run(const Work& work);
  bool resolveTarget(const char* id, std::uint64_t& node_id, std::uint16_t& endpoint_id) const;

  ModelRepository& models_;
  DeviceRegistry& devices_;
  CoreMatter& matter_;
  ActivityService& activity_;
  StaticQueue_t queue_buffer_{};
  std::uint8_t queue_storage_[kQueueDepth * sizeof(Work)]{};
  QueueHandle_t queue_{nullptr};
  StaticTask_t task_buffer_{};
  static_assert(kTaskStackBytes % sizeof(StackType_t) == 0);
  StackType_t task_stack_[kTaskStackBytes / sizeof(StackType_t)]{};
  TaskHandle_t task_{nullptr};
};

} // namespace core_pilot
