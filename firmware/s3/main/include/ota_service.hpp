#pragma once

#include "core_domain.hpp"
#include "core_ota.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cstddef>
#include <cstdint>

namespace core_pilot {

class OtaService final {
public:
  esp_err_t start();
  CoreOta::Result prepare(const core_domain::OtaPrepare& request, char (&upload_token)[65]);
  CoreOta::Result write(const core_domain::OtaChunk& request);
  CoreOta::Status status() const;
  bool deferRestart();
  bool acknowledgeUpgradeDisplay();

private:
  enum class Operation : std::uint8_t { prepare, write, restart, display_shown };

  struct RequestSlot final {
    core_domain::OtaPrepare prepare{};
    core_domain::OtaChunk chunk{};
    CoreOta::Result result{};
    CoreOta::SessionId session{};
  };

  // One serialized request plus the response and display acknowledgements.
  static constexpr UBaseType_t kQueueDepth = 3;
  static constexpr std::uint32_t kTaskStackBytes = 8 * 1024;

  static void taskEntry(void* context);
  void run();
  CoreOta::Result submit(Operation operation);

  CoreOta ota_;
  RequestSlot* slot_{};
  StaticSemaphore_t slot_mutex_storage_{};
  SemaphoreHandle_t slot_mutex_{};
  StaticSemaphore_t completion_storage_{};
  SemaphoreHandle_t completion_{};
  StaticQueue_t queue_storage_{};
  std::uint8_t queue_buffer_[kQueueDepth * sizeof(Operation)]{};
  QueueHandle_t queue_{};
  StaticTask_t task_storage_{};
  StackType_t* task_stack_{};
  TaskHandle_t task_{};
};

} // namespace core_pilot
