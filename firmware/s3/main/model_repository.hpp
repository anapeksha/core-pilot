#pragma once

#include "core_security.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "models_codec.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace core_pilot {

class ModelRepository final {
public:
  enum class Mutation { create, update, remove, set_automation_enabled, reset_unreadable };
  struct Result final {
    core_security::Status status{core_security::Status::Invalid};
    char id[core_domain::max_id + 1]{};
    bool found{false};
  };
  using Visitor = bool (*)(const core_domain::ModelUpsert& model, void* context);

  ModelRepository(core_security::StorageService& storage, core_security::RandomSource& random);
  ModelRepository(const ModelRepository&) = delete;
  ModelRepository& operator=(const ModelRepository&) = delete;

  core_security::Status start();
  bool writable() const {
    return writable_.load();
  }
  std::size_t count(core_domain::ModelKind kind) const;
  bool get(core_domain::ModelKind kind, std::size_t index, core_domain::ModelUpsert& model) const;
  bool find(core_domain::ModelKind kind, const char* id, core_domain::ModelUpsert& model) const;
  // Visits one coherent in-memory generation while holding the state mutex. Visitors must not
  // call back into ModelRepository or perform storage I/O.
  bool visit(core_domain::ModelKind kind, Visitor visitor, void* context,
             std::size_t& visited) const;
  bool submit(Mutation mutation, core_domain::ModelKind kind, const core_domain::ModelUpsert& model,
              Result& result);
  bool resetUnreadable(Result& result);

private:
  static constexpr std::size_t kQueueDepth = 4;
  static constexpr std::size_t kTaskStackBytes = 8 * 1024;
  struct WorkSlot final {
    Mutation mutation{Mutation::create};
    core_domain::ModelKind kind{core_domain::ModelKind::room};
    core_domain::ModelUpsert input{};
    Result result{};
    StaticSemaphore_t completion_buffer{};
    SemaphoreHandle_t completion{nullptr};
    bool occupied{false};
  };
  static_assert(sizeof(core_domain::ModelUpsert) <= 4096);
  static_assert(sizeof(Result) <= 64);
  static_assert(sizeof(WorkSlot) * kQueueDepth <= 20 * 1024);

  static void taskEntry(void* context);
  void taskLoop();
  void process(WorkSlot& slot);
  void reset(WorkSlot& slot);
  core_security::Status persist(const core_storage::ModelsDatabase& database,
                                std::uint64_t generation);

  core_security::StorageService& storage_;
  core_security::RandomSource& random_;
  core_storage::ModelsDatabase* databases_[2]{nullptr, nullptr};
  std::uint8_t active_{0};
  std::uint64_t generation_{0};
  mutable StaticSemaphore_t state_mutex_buffer_{};
  mutable SemaphoreHandle_t state_mutex_{nullptr};
  portMUX_TYPE slots_lock_ = portMUX_INITIALIZER_UNLOCKED;
  WorkSlot slots_[kQueueDepth]{};
  StaticQueue_t queue_buffer_{};
  std::uint8_t queue_storage_[kQueueDepth * sizeof(std::uint8_t)]{};
  QueueHandle_t queue_{nullptr};
  StaticTask_t task_buffer_{};
  static_assert(kTaskStackBytes % sizeof(StackType_t) == 0);
  StackType_t task_stack_[kTaskStackBytes / sizeof(StackType_t)]{};
  TaskHandle_t task_{nullptr};
  std::uint8_t* plaintext_{nullptr};
  std::uint8_t* encrypted_{nullptr};
  std::atomic_bool writable_{true};
};

} // namespace core_pilot
