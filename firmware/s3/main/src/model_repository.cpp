#include "model_repository.hpp"

#include "esp_heap_caps.h"
#include "mbedtls/platform_util.h"

#include <cstdio>
#include <cstring>

namespace core_pilot {
namespace {
constexpr char kPath[] = "/core-pilot/models/database.enc";
constexpr char kArchivePath[] = "/core-pilot/models/database-unreadable.enc";
constexpr core_security::RecordSpec recordSpec(std::uint64_t generation) {
  return {kPath,
          sizeof(kPath) - 1,
          core_security::Namespace::Configuration,
          core_security::RecordType::ModelsDatabase,
          core_storage::kModelsSchema,
          generation};
}

class SemaphoreLock final {
public:
  explicit SemaphoreLock(SemaphoreHandle_t semaphore)
      : semaphore_(semaphore),
        held_(semaphore && xSemaphoreTake(semaphore, portMAX_DELAY) == pdTRUE) {}
  ~SemaphoreLock() {
    if (held_)
      xSemaphoreGive(semaphore_);
  }
  bool held() const {
    return held_;
  }

private:
  SemaphoreHandle_t semaphore_;
  bool held_;
};
} // namespace

ModelRepository::ModelRepository(core_security::StorageService& storage,
                                 core_security::RandomSource& random)
    : storage_(storage), random_(random) {
  constexpr std::uint32_t capabilities = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  databases_[0] = static_cast<core_storage::ModelsDatabase*>(
      heap_caps_calloc(1, sizeof(core_storage::ModelsDatabase), capabilities));
  databases_[1] = static_cast<core_storage::ModelsDatabase*>(
      heap_caps_calloc(1, sizeof(core_storage::ModelsDatabase), capabilities));
  plaintext_ = static_cast<std::uint8_t*>(
      heap_caps_calloc(1, core_storage::kMaxModelsPayload, capabilities));
  encrypted_ = static_cast<std::uint8_t*>(
      heap_caps_calloc(1, core_storage::kMaxModelsPayload + 128, capabilities));
  state_mutex_ = xSemaphoreCreateMutexStatic(&state_mutex_buffer_);
  queue_ = xQueueCreateStatic(kQueueDepth, sizeof(std::uint8_t), queue_storage_, &queue_buffer_);
  for (auto& slot : slots_)
    slot.completion = xSemaphoreCreateBinaryStatic(&slot.completion_buffer);
}

core_security::Status ModelRepository::start() {
  if (!databases_[0] || !databases_[1] || !plaintext_ || !encrypted_ || !state_mutex_ || !queue_)
    return core_security::Status::BufferTooSmall;
  std::size_t size = 0;
  core_security::Metadata metadata{};
  auto result = storage_.get(recordSpec(0), encrypted_, core_storage::kMaxModelsPayload + 128,
                             plaintext_, core_storage::kMaxModelsPayload, size, metadata);
  if (result == core_security::Status::NotFound) {
    result = core_security::Status::Ok;
  } else if (result == core_security::Status::Ok) {
    result = core_storage::decode_models(metadata.schema_version, plaintext_, size, *databases_[0]);
    if (result == core_security::Status::Ok)
      generation_ = metadata.generation;
  }
  mbedtls_platform_zeroize(plaintext_, core_storage::kMaxModelsPayload);
  if (result != core_security::Status::Ok)
    writable_.store(false);
  task_ = xTaskCreateStaticPinnedToCore(taskEntry, "model_repository",
                                        kTaskStackBytes / sizeof(StackType_t), this, 4, task_stack_,
                                        &task_buffer_, 0);
  return task_ ? result : core_security::Status::IoError;
}

std::size_t ModelRepository::count(core_domain::ModelKind kind) const {
  SemaphoreLock lock(state_mutex_);
  if (!lock.held() || databases_[active_] == nullptr)
    return 0;
  return databases_[active_]->collections[core_storage::model_kind_index(kind)].count;
}

bool ModelRepository::get(core_domain::ModelKind kind, std::size_t index,
                          core_domain::ModelUpsert& model) const {
  SemaphoreLock lock(state_mutex_);
  if (!lock.held() || databases_[active_] == nullptr)
    return false;
  const auto& collection = databases_[active_]->collections[core_storage::model_kind_index(kind)];
  if (index >= collection.count)
    return false;
  model = collection.entries[index];
  return true;
}

bool ModelRepository::find(core_domain::ModelKind kind, const char* id,
                           core_domain::ModelUpsert& model) const {
  if (id == nullptr)
    return false;
  SemaphoreLock lock(state_mutex_);
  if (!lock.held() || databases_[active_] == nullptr)
    return false;
  const auto& collection = databases_[active_]->collections[core_storage::model_kind_index(kind)];
  for (std::size_t index = 0; index < collection.count; ++index) {
    if (std::strcmp(collection.entries[index].id, id) == 0) {
      model = collection.entries[index];
      return true;
    }
  }
  return false;
}

bool ModelRepository::visit(core_domain::ModelKind kind, Visitor visitor, void* context,
                            std::size_t& visited) const {
  visited = 0;
  if (visitor == nullptr)
    return false;
  SemaphoreLock lock(state_mutex_);
  if (!lock.held() || databases_[active_] == nullptr)
    return false;
  const auto& collection = databases_[active_]->collections[core_storage::model_kind_index(kind)];
  for (; visited < collection.count; ++visited)
    if (!visitor(collection.entries[visited], context))
      return false;
  return true;
}

bool ModelRepository::resetUnreadable(Result& result) {
  return submit(Mutation::reset_unreadable, core_domain::ModelKind::room, {}, result);
}

bool ModelRepository::submit(Mutation mutation, core_domain::ModelKind kind,
                             const core_domain::ModelUpsert& model, Result& result) {
  if (!queue_ || !task_)
    return false;
  std::uint8_t slot_index = kQueueDepth;
  portENTER_CRITICAL(&slots_lock_);
  for (std::uint8_t index = 0; index < kQueueDepth; ++index) {
    if (!slots_[index].occupied) {
      slots_[index].occupied = true;
      slot_index = index;
      break;
    }
  }
  portEXIT_CRITICAL(&slots_lock_);
  if (slot_index == kQueueDepth)
    return false;
  auto& slot = slots_[slot_index];
  slot.mutation = mutation;
  slot.kind = kind;
  slot.input = model;
  slot.result = {};
  while (xSemaphoreTake(slot.completion, 0) == pdTRUE) {
  }
  if (xQueueSend(queue_, &slot_index, 0) != pdTRUE) {
    portENTER_CRITICAL(&slots_lock_);
    slot.occupied = false;
    portEXIT_CRITICAL(&slots_lock_);
    return false;
  }
  xSemaphoreTake(slot.completion, portMAX_DELAY);
  result = slot.result;
  portENTER_CRITICAL(&slots_lock_);
  slot.occupied = false;
  portEXIT_CRITICAL(&slots_lock_);
  return true;
}

void ModelRepository::taskEntry(void* context) {
  static_cast<ModelRepository*>(context)->taskLoop();
}

void ModelRepository::taskLoop() {
  while (true) {
    std::uint8_t index = 0;
    if (xQueueReceive(queue_, &index, portMAX_DELAY) == pdTRUE && index < kQueueDepth) {
      process(slots_[index]);
      xSemaphoreGive(slots_[index].completion);
    }
  }
}

void ModelRepository::process(WorkSlot& slot) {
  if (slot.mutation == Mutation::reset_unreadable) {
    reset(slot);
    return;
  }
  slot.result = {};
  if (!writable_.load()) {
    slot.result.status = core_security::Status::UnsupportedFormat;
    return;
  }
  const std::uint8_t staging = active_ == 0 ? 1 : 0;
  {
    SemaphoreLock lock(state_mutex_);
    if (!lock.held()) {
      slot.result.status = core_security::Status::IoError;
      return;
    }
    *databases_[staging] = *databases_[active_];
  }
  auto& collection = databases_[staging]->collections[core_storage::model_kind_index(slot.kind)];
  std::size_t index = collection.count;
  for (std::size_t current = 0; current < collection.count; ++current)
    if (std::strcmp(collection.entries[current].id, slot.input.id) == 0) {
      index = current;
      break;
    }

  if (slot.mutation == Mutation::remove) {
    if (index == collection.count) {
      slot.result.status = core_security::Status::NotFound;
      return;
    }
    std::memcpy(slot.result.id, collection.entries[index].id, sizeof(slot.result.id));
    for (std::size_t current = index + 1; current < collection.count; ++current)
      collection.entries[current - 1] = collection.entries[current];
    collection.entries[--collection.count] = {};
  } else if (slot.mutation == Mutation::set_automation_enabled) {
    if (slot.kind != core_domain::ModelKind::automation || index == collection.count) {
      slot.result.status = core_security::Status::NotFound;
      return;
    }
    collection.entries[index].enabled = slot.input.enabled;
    std::memcpy(slot.result.id, collection.entries[index].id, sizeof(slot.result.id));
  } else {
    if (slot.mutation == Mutation::create && slot.input.has_id) {
      slot.result.status = core_security::Status::Invalid;
      return;
    }
    if (slot.mutation == Mutation::update && (!slot.input.has_id || index == collection.count)) {
      slot.result.status = core_security::Status::NotFound;
      return;
    }
    if (slot.mutation == Mutation::create) {
      if (collection.count == core_domain::max_models) {
        slot.result.status = core_security::Status::TooLarge;
        return;
      }
      static constexpr const char* prefixes[] = {"room", "group", "scene", "automation"};
      bool unique = false;
      for (std::size_t suffix = 0; suffix <= core_domain::max_models && !unique; ++suffix) {
        std::snprintf(slot.input.id, sizeof(slot.input.id), "%s-%016llx-%02x",
                      prefixes[core_storage::model_kind_index(slot.kind)],
                      static_cast<unsigned long long>(generation_ + 1),
                      static_cast<unsigned>(suffix));
        unique = true;
        for (std::size_t current = 0; current < collection.count; ++current)
          unique = unique && std::strcmp(slot.input.id, collection.entries[current].id) != 0;
      }
      if (!unique) {
        slot.result.status = core_security::Status::TooLarge;
        return;
      }
      slot.input.has_id = true;
      index = collection.count++;
    }
    collection.entries[index] = slot.input;
    std::memcpy(slot.result.id, slot.input.id, sizeof(slot.result.id));
  }

  const auto result = persist(*databases_[staging], generation_ + 1);
  slot.result.status = result;
  if (result != core_security::Status::Ok)
    return;
  {
    SemaphoreLock lock(state_mutex_);
    if (!lock.held()) {
      slot.result.status = core_security::Status::IoError;
      writable_.store(false);
      return;
    }
    active_ = staging;
    ++generation_;
  }
  slot.result.found = true;
}

void ModelRepository::reset(WorkSlot& slot) {
  static constexpr std::uint8_t empty_database[] = {'M', 'D', 'B', '1', 0, 0, 0, 0};
  std::uint8_t encrypted[256]{};
  auto result = storage_.archive_and_put(recordSpec(1), kArchivePath, sizeof(kArchivePath) - 1,
                                         empty_database, sizeof(empty_database), random_, encrypted,
                                         sizeof(encrypted));
  // A previous recovery may already have preserved the unreadable ciphertext.
  // In that case replace only the active record; never overwrite its quarantine copy.
  if (result == core_security::Status::Invalid) {
    result = storage_.put(recordSpec(1), empty_database, sizeof(empty_database), random_, encrypted,
                          sizeof(encrypted));
  }
  mbedtls_platform_zeroize(encrypted, sizeof(encrypted));
  slot.result.status = result;
  if (result != core_security::Status::Ok)
    return;
  SemaphoreLock lock(state_mutex_);
  if (!lock.held()) {
    writable_.store(false);
    slot.result.status = core_security::Status::IoError;
    return;
  }
  for (auto& collection : databases_[active_]->collections)
    collection.count = 0;
  generation_ = 1;
  writable_.store(true);
  slot.result.found = true;
}

core_security::Status ModelRepository::persist(const core_storage::ModelsDatabase& database,
                                               std::uint64_t generation) {
  std::size_t written = 0;
  auto result =
      core_storage::encode_models(database, plaintext_, core_storage::kMaxModelsPayload, written);
  if (result == core_security::Status::Ok)
    result = storage_.put(recordSpec(generation), plaintext_, written, random_, encrypted_,
                          core_storage::kMaxModelsPayload + 128);
  mbedtls_platform_zeroize(plaintext_, core_storage::kMaxModelsPayload);
  return result;
}

} // namespace core_pilot
