#include "smtp_service.hpp"

#include "core_activity.hpp"
#include "esp_heap_caps.h"
#include "mbedtls/platform_util.h"

#include <cstring>

namespace core_pilot {

SmtpService::SmtpService(core_security::StorageService& storage,
                         core_security::RandomSource& random, ActivityService& activity)
    : repository_(storage, random), activity_(activity) {}

SmtpService::~SmtpService() {
  if (slots_ != nullptr) {
    mbedtls_platform_zeroize(slots_, sizeof(Work) * kQueueDepth);
    heap_caps_free(slots_);
  }
  if (task_stack_ != nullptr) {
    mbedtls_platform_zeroize(task_stack_, kTaskStackBytes);
    heap_caps_free(task_stack_);
  }
}

esp_err_t SmtpService::start() {
  if (started_)
    return ESP_OK;
  if (repository_.start() != smtp::Status::Ok)
    return ESP_FAIL;
  started_ = true;
  return ESP_OK;
}

esp_err_t SmtpService::startWorker() {
  if (task_ != nullptr)
    return ESP_OK;
  slots_ = static_cast<Work*>(
      heap_caps_calloc(kQueueDepth, sizeof(Work), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  task_stack_ = static_cast<StackType_t*>(heap_caps_calloc(kTaskStackBytes / sizeof(StackType_t),
                                                           sizeof(StackType_t),
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  queue_ = xQueueCreateStatic(kQueueDepth, sizeof(std::uint8_t), queue_storage_, &queue_buffer_);
  if (slots_ == nullptr || task_stack_ == nullptr || queue_ == nullptr) {
    if (slots_ != nullptr)
      heap_caps_free(slots_);
    if (task_stack_ != nullptr)
      heap_caps_free(task_stack_);
    slots_ = nullptr;
    task_stack_ = nullptr;
    queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  task_ = xTaskCreateStaticPinnedToCore(taskEntry, "smtp", kTaskStackBytes / sizeof(StackType_t),
                                        this, 3, task_stack_, &task_buffer_, 0);
  if (task_ == nullptr) {
    mbedtls_platform_zeroize(slots_, sizeof(Work) * kQueueDepth);
    mbedtls_platform_zeroize(task_stack_, kTaskStackBytes);
    heap_caps_free(slots_);
    heap_caps_free(task_stack_);
    slots_ = nullptr;
    task_stack_ = nullptr;
    queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

smtp::Status SmtpService::save(smtp::Config& config) {
  return repository_.save(config);
}

SmtpService::Status SmtpService::status() {
  Status result{};
  result.service_ready = started_;
  result.repository_status = repository_.load(result.config);
  return result;
}

SmtpService::SubmitResult SmtpService::submit(const smtp::Message& message) {
  if (!started_ || startWorker() != ESP_OK)
    return SubmitResult::not_ready;
  smtp::Config config{};
  const smtp::Status loaded = repository_.load(config);
  const bool configured = loaded == smtp::Status::Ok && config.enabled;
  smtp::clear_config(config);
  if (!configured)
    return SubmitResult::not_configured;
  if (message.body == nullptr || message.body_length == 0 ||
      message.body_length > smtp::kMaxBodyBytes || message.subject[0] == '\0' ||
      smtp::contains_cr_or_lf(message.subject, smtp::kMaxSubjectBytes) ||
      message.recipient_count > smtp::kMaxRecipients)
    return SubmitResult::invalid;
  for (std::size_t index = 0; index < message.recipient_count; ++index)
    if (!smtp::valid_email_address(message.recipients[index]))
      return SubmitResult::invalid;

  std::uint8_t slot = kQueueDepth;
  taskENTER_CRITICAL(&slots_lock_);
  for (std::uint8_t index = 0; index < kQueueDepth; ++index) {
    if (!occupied_[index]) {
      occupied_[index] = true;
      slot = index;
      break;
    }
  }
  taskEXIT_CRITICAL(&slots_lock_);
  if (slot == kQueueDepth)
    return SubmitResult::queue_full;

  Work& work = slots_[slot];
  std::memcpy(work.subject, message.subject, std::strlen(message.subject) + 1);
  std::memcpy(work.body, message.body, message.body_length);
  work.body[message.body_length] = '\0';
  work.body_length = message.body_length;
  work.recipient_count = message.recipient_count;
  for (std::size_t index = 0; index < message.recipient_count; ++index)
    std::memcpy(work.recipients[index], message.recipients[index],
                std::strlen(message.recipients[index]) + 1);

  if (xQueueSend(queue_, &slot, 0) != pdTRUE) {
    mbedtls_platform_zeroize(&work, sizeof(work));
    taskENTER_CRITICAL(&slots_lock_);
    occupied_[slot] = false;
    taskEXIT_CRITICAL(&slots_lock_);
    return SubmitResult::queue_full;
  }
  activity_.append("smtp_test_queued", "An SMTP test message was queued.");
  return SubmitResult::queued;
}

void SmtpService::taskEntry(void* context) {
  static_cast<SmtpService*>(context)->taskLoop();
}

void SmtpService::taskLoop() {
  std::uint8_t slot{};
  while (true) {
    if (xQueueReceive(queue_, &slot, portMAX_DELAY) == pdTRUE && slot < kQueueDepth) {
      run(slots_[slot]);
      mbedtls_platform_zeroize(&slots_[slot], sizeof(Work));
      taskENTER_CRITICAL(&slots_lock_);
      occupied_[slot] = false;
      taskEXIT_CRITICAL(&slots_lock_);
    }
  }
}

void SmtpService::run(Work& work) {
  smtp::Config config{};
  const smtp::Status loaded = repository_.load(config);
  if (loaded != smtp::Status::Ok || !config.enabled) {
    smtp::clear_config(config);
    activity_.append("smtp_test_failed", "SMTP test could not use a valid enabled configuration.");
    return;
  }
  smtp::Message message{};
  std::memcpy(message.subject, work.subject, sizeof(message.subject));
  message.body = work.body;
  message.body_length = work.body_length;
  message.recipient_count = work.recipient_count;
  std::memcpy(message.recipients, work.recipients, sizeof(message.recipients));
  const smtp::Status result = smtp::send(config, message);
  smtp::clear_config(config);
  mbedtls_platform_zeroize(&message, sizeof(message));
  activity_.append(result == smtp::Status::Ok ? "smtp_test_sent" : "smtp_test_failed",
                   result == smtp::Status::Ok ? "SMTP test message was sent."
                                              : "SMTP test message delivery failed.");
}

} // namespace core_pilot
