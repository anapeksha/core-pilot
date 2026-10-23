#pragma once

#include "core_security.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace core_pilot {

class ActivityService final {
public:
  static constexpr std::size_t kCapacity = 128;
  static constexpr std::size_t kMaxKindBytes = 31;
  static constexpr std::size_t kMaxDetailBytes = 95;
  static constexpr std::uint8_t kMaxPageSize = 32;

  struct Event final {
    std::uint32_t sequence{};
    std::int64_t unix_seconds{};
    std::uint32_t uptime_seconds{};
    char kind[kMaxKindBytes + 1]{};
    char detail[kMaxDetailBytes + 1]{};
  };

  struct Page final {
    Event events[kMaxPageSize]{};
    std::uint8_t count{};
    bool has_next{};
    std::uint32_t next_before_sequence{};
    std::uint16_t retained{};
    std::uint32_t dropped_events{};
  };

  ActivityService(core_security::StorageService& storage, core_security::RandomSource& random);
  ~ActivityService();
  ActivityService(const ActivityService&) = delete;
  ActivityService& operator=(const ActivityService&) = delete;

  core_security::Status start();
  bool append(const char* kind, const char* detail);
  bool page(bool has_before_sequence, std::uint32_t before_sequence, std::uint8_t limit,
            Page& output) const;

private:
  static constexpr std::size_t kQueueDepth = 16;
  static constexpr std::size_t kTaskStackBytes = 4096;
  static constexpr TickType_t kFlushDelay = pdMS_TO_TICKS(5000);

  struct PendingEvent final {
    char kind[kMaxKindBytes + 1]{};
    char detail[kMaxDetailBytes + 1]{};
  };

  static void taskEntry(void* context);
  void taskLoop();
  void insert(const PendingEvent& pending);
  core_security::Status load();
  core_security::Status persist();

  core_security::StorageService& storage_;
  core_security::RandomSource& random_;
  Event* events_{nullptr};
  std::uint16_t count_{};
  std::uint16_t head_{};
  std::uint32_t next_sequence_{1};
  std::uint64_t generation_{};
  std::atomic_uint32_t dropped_events_;
  mutable StaticSemaphore_t mutex_buffer_{};
  mutable SemaphoreHandle_t mutex_{nullptr};
  StaticQueue_t queue_buffer_{};
  std::uint8_t queue_storage_[kQueueDepth * sizeof(PendingEvent)]{};
  QueueHandle_t queue_{nullptr};
  StaticTask_t task_buffer_{};
  static_assert(kTaskStackBytes % sizeof(StackType_t) == 0);
  StackType_t task_stack_[kTaskStackBytes / sizeof(StackType_t)]{};
  TaskHandle_t task_{nullptr};
  std::uint8_t* plaintext_{nullptr};
  std::uint8_t* encrypted_{nullptr};
};

} // namespace core_pilot
