#pragma once

#include "core_smtp.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <cstddef>
#include <cstdint>

namespace core_pilot {

class ActivityService;

class SmtpService final {
public:
  enum class SubmitResult : std::uint8_t { queued, not_configured, not_ready, invalid, queue_full };

  struct Status final {
    smtp::Config config{};
    smtp::Status repository_status{smtp::Status::NotReady};
    bool service_ready{};
  };

  SmtpService(core_security::StorageService& storage, core_security::RandomSource& random,
              ActivityService& activity);
  ~SmtpService();
  SmtpService(const SmtpService&) = delete;
  SmtpService& operator=(const SmtpService&) = delete;

  esp_err_t start();
  smtp::Status save(smtp::Config& config);
  Status status();
  SubmitResult submit(const smtp::Message& message);

private:
  static constexpr std::size_t kQueueDepth = 2;
  static constexpr std::size_t kTaskStackBytes = 8192;

  struct Work final {
    char subject[smtp::kMaxSubjectBytes + 1]{};
    char body[smtp::kMaxBodyBytes + 1]{};
    std::size_t body_length{};
    std::uint8_t recipient_count{};
    char recipients[smtp::kMaxRecipients][smtp::kMaxEmailBytes + 1]{};
  };

  static void taskEntry(void* context);
  esp_err_t startWorker();
  void taskLoop();
  void run(Work& work);

  smtp::ConfigRepository repository_;
  ActivityService& activity_;
  portMUX_TYPE slots_lock_ = portMUX_INITIALIZER_UNLOCKED;
  Work* slots_{};
  bool occupied_[kQueueDepth]{};
  StaticQueue_t queue_buffer_{};
  std::uint8_t queue_storage_[kQueueDepth * sizeof(std::uint8_t)]{};
  QueueHandle_t queue_{};
  StaticTask_t task_buffer_{};
  StackType_t* task_stack_{};
  TaskHandle_t task_{};
  bool started_{};
};

} // namespace core_pilot
