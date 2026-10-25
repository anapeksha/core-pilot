#include "ota_service.hpp"

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"

#include <cstring>

namespace core_pilot {
namespace {

class SemaphoreLock final {
public:
  explicit SemaphoreLock(SemaphoreHandle_t semaphore)
      : semaphore_(semaphore),
        held_(semaphore != nullptr && xSemaphoreTake(semaphore, 0) == pdTRUE) {}
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

CoreOta::Result unavailableResult() {
  return {CoreOta::Error::invalid_state, ESP_ERR_INVALID_STATE};
}

} // namespace

esp_err_t OtaService::start() {
  if (task_ != nullptr)
    return ESP_OK;
  if (slot_ == nullptr)
    slot_ = static_cast<RequestSlot*>(
        heap_caps_calloc(1, sizeof(RequestSlot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  slot_mutex_ = xSemaphoreCreateMutexStatic(&slot_mutex_storage_);
  completion_ = xSemaphoreCreateBinaryStatic(&completion_storage_);
  queue_ = xQueueCreateStatic(kQueueDepth, sizeof(Operation), queue_buffer_, &queue_storage_);
  if (slot_ == nullptr || slot_mutex_ == nullptr || completion_ == nullptr || queue_ == nullptr)
    return ESP_ERR_NO_MEM;
  task_stack_ = static_cast<StackType_t*>(heap_caps_calloc(kTaskStackBytes / sizeof(StackType_t),
                                                           sizeof(StackType_t),
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (task_stack_ == nullptr)
    return ESP_ERR_NO_MEM;
  task_ = xTaskCreateStaticPinnedToCore(taskEntry, "ota", kTaskStackBytes / sizeof(StackType_t),
                                        this, 4, task_stack_, &task_storage_, 0);
  if (task_ == nullptr) {
    heap_caps_free(task_stack_);
    task_stack_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

CoreOta::Result OtaService::prepare(const core_domain::OtaPrepare& request) {
  SemaphoreLock lock(slot_mutex_);
  if (!lock.held())
    return unavailableResult();
  slot_->prepare = request;
  return submit(Operation::prepare);
}

CoreOta::Result OtaService::write(const core_domain::OtaChunk& request) {
  SemaphoreLock lock(slot_mutex_);
  if (!lock.held())
    return unavailableResult();
  slot_->chunk = request;
  const CoreOta::Result result = submit(Operation::write);
  mbedtls_platform_zeroize(&slot_->chunk, sizeof slot_->chunk);
  return result;
}

CoreOta::Result OtaService::submit(Operation operation) {
  if (task_ == nullptr || queue_ == nullptr || completion_ == nullptr ||
      xQueueSend(queue_, &operation, 0) != pdTRUE)
    return unavailableResult();
  if (xSemaphoreTake(completion_, portMAX_DELAY) != pdTRUE)
    return unavailableResult();
  return slot_->result;
}

CoreOta::Status OtaService::status() const {
  return ota_.status();
}

bool OtaService::deferRestart() {
  const Operation operation = Operation::restart;
  return task_ != nullptr && queue_ != nullptr && xQueueSend(queue_, &operation, 0) == pdTRUE;
}

void OtaService::taskEntry(void* context) {
  static_cast<OtaService*>(context)->run();
}

void OtaService::run() {
  while (true) {
    Operation operation{};
    if (xQueueReceive(queue_, &operation, pdMS_TO_TICKS(1000)) != pdTRUE) {
      // status() owns the timeout transition and aborts stale OTA handles.
      ota_.status();
      continue;
    }
    if (operation == Operation::restart) {
      vTaskDelay(pdMS_TO_TICKS(500));
      esp_restart();
    }

    if (operation == Operation::prepare) {
      CoreOta::Prepare request{};
      std::memcpy(request.version, slot_->prepare.version, sizeof request.version);
      request.size_bytes = slot_->prepare.size_bytes;
      std::memcpy(request.sha256_hex, slot_->prepare.sha256_hex, sizeof request.sha256_hex);
      request.restart_after_update = slot_->prepare.restart_after_update;
      slot_->result = ota_.prepare(request);
      mbedtls_platform_zeroize(&request, sizeof request);
      xSemaphoreGive(completion_);
      continue;
    }

    std::uint8_t decoded[CoreOta::kMaxChunkBytes]{};
    std::size_t decoded_size = 0;
    const auto status = ota_.status();
    const auto validation = core_domain::validate_ota_chunk(slot_->chunk, status.written_bytes,
                                                            status.size_bytes, decoded_size);
    int decode_result = MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
    if (validation == core_domain::OtaError::ok) {
      decode_result =
          mbedtls_base64_decode(decoded, sizeof decoded, &decoded_size,
                                reinterpret_cast<const unsigned char*>(slot_->chunk.data_base64),
                                std::strlen(slot_->chunk.data_base64));
    }
    if (validation != core_domain::OtaError::ok || decode_result != 0 || decoded_size == 0) {
      slot_->result = {
          validation == core_domain::OtaError::chunk_too_large   ? CoreOta::Error::chunk_too_large
          : validation == core_domain::OtaError::offset_mismatch ? CoreOta::Error::offset_mismatch
          : validation == core_domain::OtaError::image_too_large ? CoreOta::Error::image_too_large
                                                                 : CoreOta::Error::invalid_argument,
          ESP_ERR_INVALID_ARG};
    } else {
      slot_->result =
          ota_.write({slot_->chunk.offset, decoded, decoded_size, slot_->chunk.final_chunk});
    }
    mbedtls_platform_zeroize(decoded, sizeof decoded);
    xSemaphoreGive(completion_);
  }
}

} // namespace core_pilot
