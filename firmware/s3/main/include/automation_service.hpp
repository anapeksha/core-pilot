#pragma once

#include "automation_schedule_codec.hpp"
#include "core_domain.hpp"
#include "core_security.hpp"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <type_traits>

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
    scene_not_found,
    scene_cycle,
    scene_depth_exceeded,
    too_many_actions,
  };

  enum class State : std::uint8_t { queued, running, complete, failed };
  enum class Failure : std::uint8_t {
    none,
    invalid_action,
    target_unavailable,
    matter_rejected,
    matter_timeout,
    matter_failed,
    notification_rejected,
  };

  struct Status final {
    std::uint32_t operation_id{};
    State state{State::queued};
    Failure failure{Failure::none};
    std::uint8_t action_index{};
    std::uint8_t action_count{};
    esp_err_t last_error{ESP_OK};
  };

  AutomationService(ModelRepository& models, DeviceRegistry& devices, CoreMatter& matter,
                    ActivityService& activity, core_security::StorageService& storage,
                    core_security::RandomSource& random);
  esp_err_t start();
  void timeSynchronized(std::time_t timestamp);
  SubmitResult submit(const char* id, std::uint32_t& operation_id);
  SubmitResult submit(const char* id);
  bool status(std::uint32_t operation_id, Status& output) const;

private:
  static constexpr std::size_t kQueueDepth = 4;
  static constexpr std::size_t kMaxActions = 64;
  static constexpr std::size_t kMaxSceneDepth = 8;
  static constexpr std::size_t kRetainedStatuses = 12;
  static constexpr std::size_t kTaskStackBytes = 8192;
  static constexpr TickType_t kMatterCommandTimeout = pdMS_TO_TICKS(30000);

  struct Work final {
    std::uint32_t operation_id{};
    std::uint8_t action_count{};
    bool scheduled{};
    core_domain::ActionSpec actions[kMaxActions]{};
  };
  using WorkSlot = std::uint8_t;
  static_assert(std::is_trivially_copyable_v<WorkSlot>);

  static void taskEntry(void* context);
  static void minuteTimerCallback(void* context);
  static bool collectScheduled(const core_domain::ModelUpsert& model, void* context);
  void taskLoop();
  void evaluateSchedules(std::time_t timestamp);
  bool armMinuteTimer(std::time_t timestamp);
  bool checkpoint(const core_domain::ModelUpsert& model, std::uint64_t utc_minute);
  SubmitResult submitModel(const core_domain::ModelUpsert& automation, bool scheduled,
                           std::uint32_t& operation_id, std::uint64_t utc_minute = 0);
  bool acquireWorkSlot(WorkSlot& slot);
  void releaseWorkSlot(WorkSlot slot);
  void run(const Work& work);
  SubmitResult flatten(const core_domain::ModelUpsert& model, Work& work,
                       char scene_stack[kMaxSceneDepth][core_domain::max_id + 1],
                       std::size_t depth);
  bool resolveTarget(const char* id, std::uint64_t& node_id, std::uint16_t& endpoint_id) const;
  bool retain(const Status& status);
  void update(std::uint32_t operation_id, State state, Failure failure, std::uint8_t action_index,
              esp_err_t error);

  ModelRepository& models_;
  DeviceRegistry& devices_;
  CoreMatter& matter_;
  ActivityService& activity_;
  core_security::StorageService& storage_;
  core_security::RandomSource& random_;
  core_storage::AutomationScheduleCheckpoints checkpoints_{};
  core_storage::AutomationScheduleCheckpoints* checkpoint_staging_{nullptr};
  core_storage::AutomationScheduleCheckpoints* schedule_candidates_{nullptr};
  Work* work_slots_{nullptr};
  bool work_slot_used_[kQueueDepth]{};
  std::uint8_t* checkpoint_plaintext_{nullptr};
  std::uint8_t* checkpoint_encrypted_{nullptr};
  std::uint64_t checkpoint_generation_{};
  std::atomic<std::time_t> synchronized_time_{0};
  esp_timer_handle_t minute_timer_{nullptr};
  StaticQueue_t queue_buffer_{};
  std::uint8_t queue_storage_[kQueueDepth * sizeof(WorkSlot)]{};
  QueueHandle_t queue_{nullptr};
  StaticTask_t task_buffer_{};
  static_assert(kTaskStackBytes % sizeof(StackType_t) == 0);
  StackType_t task_stack_[kTaskStackBytes / sizeof(StackType_t)]{};
  TaskHandle_t task_{nullptr};
  std::atomic_uint32_t next_operation_id_{1};
  mutable portMUX_TYPE status_lock_ = portMUX_INITIALIZER_UNLOCKED;
  Status statuses_[kRetainedStatuses]{};
  std::size_t next_status_{};
};

} // namespace core_pilot
