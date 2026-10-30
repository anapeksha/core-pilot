#include "automation_service.hpp"

#include "core_activity.hpp"
#include "core_matter.hpp"
#include "device_registry.hpp"
#include "esp_heap_caps.h"
#include "mbedtls/platform_util.h"
#include "model_repository.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace core_pilot {

namespace {
constexpr char kCheckpointPath[] = "/core-pilot/automations/schedule-checkpoints.enc";
constexpr std::size_t kCheckpointEncryptedBytes =
    core_storage::kMaxAutomationScheduleCheckpointsPayload + 128;

core_security::RecordSpec checkpointSpec(std::uint64_t generation) {
  return {kCheckpointPath,
          sizeof(kCheckpointPath) - 1,
          core_security::Namespace::Configuration,
          core_security::RecordType::AutomationScheduleCheckpoints,
          core_storage::kAutomationScheduleCheckpointsSchema,
          generation};
}
} // namespace

AutomationService::AutomationService(ModelRepository& models, DeviceRegistry& devices,
                                     CoreMatter& matter, ActivityService& activity,
                                     core_security::StorageService& storage,
                                     core_security::RandomSource& random)
    : models_(models), devices_(devices), matter_(matter), activity_(activity), storage_(storage),
      random_(random) {}

esp_err_t AutomationService::start() {
  if (task_ != nullptr)
    return ESP_OK;

  checkpoint_plaintext_ = static_cast<std::uint8_t*>(heap_caps_calloc(
      1, core_storage::kMaxAutomationScheduleCheckpointsPayload, MALLOC_CAP_SPIRAM));
  checkpoint_encrypted_ =
      static_cast<std::uint8_t*>(heap_caps_calloc(1, kCheckpointEncryptedBytes, MALLOC_CAP_SPIRAM));
  if (checkpoint_plaintext_ == nullptr || checkpoint_encrypted_ == nullptr) {
    heap_caps_free(checkpoint_plaintext_);
    heap_caps_free(checkpoint_encrypted_);
    checkpoint_plaintext_ = nullptr;
    checkpoint_encrypted_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  std::size_t size = 0;
  core_security::Metadata metadata{};
  const auto loaded = storage_.get(
      checkpointSpec(0), checkpoint_encrypted_, kCheckpointEncryptedBytes, checkpoint_plaintext_,
      core_storage::kMaxAutomationScheduleCheckpointsPayload, size, metadata);
  if (loaded == core_security::Status::Ok) {
    const auto decoded = core_storage::decode_automation_schedule_checkpoints(
        metadata.schema_version, checkpoint_plaintext_, size, checkpoints_);
    if (decoded != core_security::Status::Ok) {
      mbedtls_platform_zeroize(checkpoint_plaintext_,
                               core_storage::kMaxAutomationScheduleCheckpointsPayload);
      mbedtls_platform_zeroize(checkpoint_encrypted_, kCheckpointEncryptedBytes);
      return ESP_ERR_INVALID_STATE;
    }
    checkpoint_generation_ = metadata.generation;
  } else if (loaded != core_security::Status::NotFound) {
    mbedtls_platform_zeroize(checkpoint_plaintext_,
                             core_storage::kMaxAutomationScheduleCheckpointsPayload);
    mbedtls_platform_zeroize(checkpoint_encrypted_, kCheckpointEncryptedBytes);
    return ESP_ERR_INVALID_STATE;
  }

  mbedtls_platform_zeroize(checkpoint_plaintext_,
                           core_storage::kMaxAutomationScheduleCheckpointsPayload);
  mbedtls_platform_zeroize(checkpoint_encrypted_, kCheckpointEncryptedBytes);

  queue_ = xQueueCreateStatic(kQueueDepth, sizeof(Work), queue_storage_, &queue_buffer_);
  if (queue_ == nullptr)
    return ESP_ERR_NO_MEM;
  task_ =
      xTaskCreateStaticPinnedToCore(taskEntry, "automation", kTaskStackBytes / sizeof(StackType_t),
                                    this, 3, task_stack_, &task_buffer_, 0);
  if (task_ == nullptr)
    return ESP_ERR_NO_MEM;
  const esp_timer_create_args_t timer_args{.callback = minuteTimerCallback,
                                           .arg = this,
                                           .dispatch_method = ESP_TIMER_TASK,
                                           .name = "automation-minute",
                                           .skip_unhandled_events = true};
  return esp_timer_create(&timer_args, &minute_timer_);
}

void AutomationService::timeSynchronized(std::time_t timestamp) {
  if (timestamp < 1704067200 || task_ == nullptr)
    return;
  synchronized_time_.store(timestamp, std::memory_order_release);
  xTaskNotifyGive(task_);
}

AutomationService::SubmitResult AutomationService::submit(const char* id,
                                                          std::uint32_t& operation_id) {
  operation_id = 0;
  if (id == nullptr || queue_ == nullptr || task_ == nullptr)
    return SubmitResult::not_found;

  core_domain::ModelUpsert automation{};
  if (!models_.find(core_domain::ModelKind::automation, id, automation))
    return SubmitResult::not_found;
  if (!automation.enabled)
    return SubmitResult::disabled;
  core_domain::TriggerSpec trigger{};
  if (!core_domain::parse_trigger_spec(automation.trigger, trigger) ||
      trigger.kind != core_domain::TriggerSpec::Kind::manual)
    return SubmitResult::not_manual;
  return submitModel(automation, false, operation_id);
}

AutomationService::SubmitResult
AutomationService::submitModel(const core_domain::ModelUpsert& automation, bool scheduled,
                               std::uint32_t& operation_id) {
  operation_id = 0;
  if (!automation.enabled)
    return SubmitResult::disabled;
  Work work{};
  work.scheduled = scheduled;
  char scene_stack[kMaxSceneDepth][core_domain::max_id + 1]{};
  const auto flattened = flatten(automation, work, scene_stack, 0);
  if (flattened != SubmitResult::queued)
    return flattened;

  std::uint32_t candidate = next_operation_id_.fetch_add(1, std::memory_order_relaxed);
  if (candidate == 0)
    candidate = next_operation_id_.fetch_add(1, std::memory_order_relaxed);
  work.operation_id = candidate;
  retain(Status{.operation_id = candidate,
                .state = State::queued,
                .failure = Failure::none,
                .action_index = 0,
                .action_count = work.action_count,
                .last_error = ESP_OK});
  if (xQueueSend(queue_, &work, 0) != pdTRUE) {
    taskENTER_CRITICAL(&status_lock_);
    for (auto& retained : statuses_) {
      if (retained.operation_id == candidate) {
        retained = {};
        break;
      }
    }
    taskEXIT_CRITICAL(&status_lock_);
    return SubmitResult::queue_full;
  }
  operation_id = candidate;
  xTaskNotifyGive(task_);
  activity_.append("automation_queued", scheduled ? "A scheduled automation was queued."
                                                  : "A manual automation was queued.");
  return SubmitResult::queued;
}

AutomationService::SubmitResult AutomationService::submit(const char* id) {
  std::uint32_t operation_id = 0;
  return submit(id, operation_id);
}

bool AutomationService::status(std::uint32_t operation_id, Status& output) const {
  if (operation_id == 0)
    return false;
  taskENTER_CRITICAL(&status_lock_);
  for (const auto& retained : statuses_) {
    if (retained.operation_id == operation_id) {
      output = retained;
      taskEXIT_CRITICAL(&status_lock_);
      return true;
    }
  }
  taskEXIT_CRITICAL(&status_lock_);
  return false;
}

AutomationService::SubmitResult
AutomationService::flatten(const core_domain::ModelUpsert& model, Work& work,
                           char scene_stack[kMaxSceneDepth][core_domain::max_id + 1],
                           std::size_t depth) {
  for (std::size_t index = 0; index < model.detail_count; ++index) {
    core_domain::ActionSpec action{};
    if (!core_domain::parse_action_spec(model.details[index], action))
      return SubmitResult::unsupported_action;
    if (action.kind == core_domain::ActionSpec::Kind::scene) {
      if (depth == kMaxSceneDepth)
        return SubmitResult::scene_depth_exceeded;
      for (std::size_t ancestor = 0; ancestor < depth; ++ancestor) {
        if (std::strcmp(scene_stack[ancestor], action.target) == 0)
          return SubmitResult::scene_cycle;
      }
      core_domain::ModelUpsert scene{};
      if (!models_.find(core_domain::ModelKind::scene, action.target, scene))
        return SubmitResult::scene_not_found;
      std::strncpy(scene_stack[depth], action.target, core_domain::max_id);
      const auto result = flatten(scene, work, scene_stack, depth + 1);
      scene_stack[depth][0] = '\0';
      if (result != SubmitResult::queued)
        return result;
      continue;
    }
    if (action.kind == core_domain::ActionSpec::Kind::on ||
        action.kind == core_domain::ActionSpec::Kind::level) {
      std::uint64_t node_id = 0;
      std::uint16_t endpoint_id = 0;
      if (!resolveTarget(action.target, node_id, endpoint_id))
        return SubmitResult::target_unavailable;
    }
    if (work.action_count == kMaxActions)
      return SubmitResult::too_many_actions;
    work.actions[work.action_count++] = action;
  }
  return SubmitResult::queued;
}

void AutomationService::taskEntry(void* context) {
  static_cast<AutomationService*>(context)->taskLoop();
}

void AutomationService::minuteTimerCallback(void* context) {
  auto* service = static_cast<AutomationService*>(context);
  service->synchronized_time_.store(std::time(nullptr), std::memory_order_release);
  xTaskNotifyGive(service->task_);
}

bool AutomationService::collectScheduled(const core_domain::ModelUpsert& model, void* context) {
  auto* collected = static_cast<core_storage::AutomationScheduleCheckpoints*>(context);
  core_domain::TriggerSpec trigger{};
  if (!model.enabled || !core_domain::parse_trigger_spec(model.trigger, trigger) ||
      trigger.kind == core_domain::TriggerSpec::Kind::manual)
    return true;
  if (collected->count == core_storage::kMaxAutomationScheduleCheckpoints)
    return false;
  auto& entry = collected->entries[collected->count++];
  std::strncpy(entry.automation_id, model.id, core_storage::kMaxAutomationIdBytes);
  std::strncpy(entry.exact_trigger, model.trigger, core_storage::kMaxAutomationTriggerBytes);
  return true;
}

void AutomationService::taskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const std::time_t timestamp = synchronized_time_.exchange(0, std::memory_order_acq_rel);
    if (timestamp >= 1704067200)
      evaluateSchedules(timestamp);
    Work work{};
    while (xQueueReceive(queue_, &work, 0) == pdTRUE)
      run(work);
  }
}

bool AutomationService::armMinuteTimer(std::time_t timestamp) {
  if (minute_timer_ == nullptr)
    return false;
  esp_timer_stop(minute_timer_);
  const auto remainder = static_cast<std::uint64_t>(timestamp % 60);
  return esp_timer_start_once(minute_timer_, (60 - remainder) * 1000000ULL) == ESP_OK;
}

void AutomationService::evaluateSchedules(std::time_t timestamp) {
  armMinuteTimer(timestamp);
  const std::uint64_t utc_minute = static_cast<std::uint64_t>(timestamp) / 60;
  core_storage::AutomationScheduleCheckpoints candidates{};
  std::size_t visited = 0;
  if (!models_.visit(core_domain::ModelKind::automation, collectScheduled, &candidates, visited))
    return;
  for (std::size_t index = 0; index < candidates.count; ++index) {
    const auto& candidate = candidates.entries[index];
    core_domain::TriggerSpec trigger{};
    if (!core_domain::parse_trigger_spec(candidate.exact_trigger, trigger))
      continue;
    const std::time_t local_timestamp =
        timestamp + static_cast<std::time_t>(trigger.timezone_offset_minutes) * 60;
    std::tm local{};
    if (gmtime_r(&local_timestamp, &local) == nullptr)
      continue;
    const core_domain::DateTime date_time{static_cast<std::uint16_t>(local.tm_year + 1900),
                                          static_cast<std::uint8_t>(local.tm_mon + 1),
                                          static_cast<std::uint8_t>(local.tm_mday),
                                          static_cast<std::uint8_t>(local.tm_hour),
                                          static_cast<std::uint8_t>(local.tm_min),
                                          static_cast<std::uint8_t>(local.tm_wday)};
    if (!core_domain::schedule_matches(trigger, date_time))
      continue;
    core_domain::ModelUpsert model{};
    if (!models_.find(core_domain::ModelKind::automation, candidate.automation_id, model) ||
        std::strcmp(model.trigger, candidate.exact_trigger) != 0 || !checkpoint(model, utc_minute))
      continue;
    std::uint32_t operation_id = 0;
    submitModel(model, true, operation_id);
  }
}

bool AutomationService::checkpoint(const core_domain::ModelUpsert& model,
                                   std::uint64_t utc_minute) {
  std::size_t index = 0;
  while (index < checkpoints_.count &&
         (std::strcmp(checkpoints_.entries[index].automation_id, model.id) != 0 ||
          std::strcmp(checkpoints_.entries[index].exact_trigger, model.trigger) != 0))
    ++index;
  if (index < checkpoints_.count &&
      checkpoints_.entries[index].last_utc_occurrence_minute == utc_minute)
    return false;
  if (index == checkpoints_.count) {
    if (checkpoints_.count == core_storage::kMaxAutomationScheduleCheckpoints)
      return false;
    ++checkpoints_.count;
  }
  auto& entry = checkpoints_.entries[index];
  std::strncpy(entry.automation_id, model.id, core_storage::kMaxAutomationIdBytes);
  std::strncpy(entry.exact_trigger, model.trigger, core_storage::kMaxAutomationTriggerBytes);
  entry.last_utc_occurrence_minute = utc_minute;

  mbedtls_platform_zeroize(checkpoint_plaintext_,
                           core_storage::kMaxAutomationScheduleCheckpointsPayload);
  mbedtls_platform_zeroize(checkpoint_encrypted_, kCheckpointEncryptedBytes);
  std::size_t written = 0;
  const auto encoded = core_storage::encode_automation_schedule_checkpoints(
      checkpoints_, checkpoint_plaintext_, core_storage::kMaxAutomationScheduleCheckpointsPayload,
      written);
  if (encoded != core_security::Status::Ok)
    return false;
  const auto result =
      storage_.put(checkpointSpec(checkpoint_generation_ + 1), checkpoint_plaintext_, written,
                   random_, checkpoint_encrypted_, kCheckpointEncryptedBytes);
  mbedtls_platform_zeroize(checkpoint_plaintext_,
                           core_storage::kMaxAutomationScheduleCheckpointsPayload);
  mbedtls_platform_zeroize(checkpoint_encrypted_, kCheckpointEncryptedBytes);
  if (result != core_security::Status::Ok)
    return false;
  ++checkpoint_generation_;
  return true;
}

void AutomationService::run(const Work& work) {
  update(work.operation_id, State::running, Failure::none, 0, ESP_OK);
  for (std::uint8_t index = 0; index < work.action_count; ++index) {
    const auto& action = work.actions[index];
    update(work.operation_id, State::running, Failure::none, index, ESP_OK);
    if (action.kind == core_domain::ActionSpec::Kind::delay) {
      vTaskDelay(pdMS_TO_TICKS(action.value));
      continue;
    }
    if (action.kind == core_domain::ActionSpec::Kind::notify) {
      if (!activity_.append("automation_notification", action.target)) {
        update(work.operation_id, State::failed, Failure::notification_rejected, index, ESP_FAIL);
        return;
      }
      continue;
    }

    std::uint64_t node_id = 0;
    std::uint16_t endpoint_id = 0;
    if (!resolveTarget(action.target, node_id, endpoint_id)) {
      update(work.operation_id, State::failed, Failure::target_unavailable, index,
             ESP_ERR_NOT_FOUND);
      return;
    }
    std::uint32_t matter_operation_id = 0;
    esp_err_t submitted = ESP_ERR_INVALID_ARG;
    if (action.kind == core_domain::ActionSpec::Kind::on) {
      submitted = matter_.setOnOff(node_id, endpoint_id, action.value != 0, matter_operation_id);
    } else if (action.kind == core_domain::ActionSpec::Kind::level) {
      const auto level = static_cast<std::uint8_t>((action.value * 254U + 50U) / 100U);
      submitted = matter_.setLevel(node_id, endpoint_id, level, 0, matter_operation_id);
    } else {
      update(work.operation_id, State::failed, Failure::invalid_action, index, ESP_ERR_INVALID_ARG);
      return;
    }
    if (submitted != ESP_OK) {
      update(work.operation_id, State::failed, Failure::matter_rejected, index, submitted);
      return;
    }

    CoreMatter::TerminalCommand terminal{};
    const esp_err_t waited =
        matter_.waitForTerminalCommand(matter_operation_id, kMatterCommandTimeout, terminal);
    if (waited != ESP_OK) {
      update(work.operation_id, State::failed, Failure::matter_timeout, index, waited);
      return;
    }
    if (terminal.status.state != CoreMatter::CommandState::complete) {
      update(work.operation_id, State::failed, Failure::matter_failed, index,
             terminal.status.last_error);
      return;
    }
  }
  update(work.operation_id, State::complete, Failure::none, work.action_count, ESP_OK);
  activity_.append("automation_complete", work.scheduled
                                              ? "A scheduled automation completed successfully."
                                              : "A manual automation completed successfully.");
}

void AutomationService::retain(const Status& status_value) {
  taskENTER_CRITICAL(&status_lock_);
  statuses_[next_status_] = status_value;
  next_status_ = (next_status_ + 1) % kRetainedStatuses;
  taskEXIT_CRITICAL(&status_lock_);
}

void AutomationService::update(std::uint32_t operation_id, State state, Failure failure,
                               std::uint8_t action_index, esp_err_t error) {
  taskENTER_CRITICAL(&status_lock_);
  for (auto& retained : statuses_) {
    if (retained.operation_id == operation_id) {
      retained.state = state;
      retained.failure = failure;
      retained.action_index = action_index;
      retained.last_error = error;
      break;
    }
  }
  taskEXIT_CRITICAL(&status_lock_);
  if (state == State::failed)
    activity_.append("automation_failed", "A manual automation operation failed.");
}

bool AutomationService::resolveTarget(const char* id, std::uint64_t& node_id,
                                      std::uint16_t& endpoint_id) const {
  if (id == nullptr || std::strlen(id) < 18 || id[16] != '-')
    return false;
  char node[17]{};
  std::memcpy(node, id, 16);
  errno = 0;
  char* node_end = nullptr;
  const auto parsed_node = std::strtoull(node, &node_end, 16);
  char* endpoint_end = nullptr;
  const auto parsed_endpoint = std::strtoul(id + 17, &endpoint_end, 10);
  if (errno != 0 || node_end == nullptr || *node_end != '\0' || endpoint_end == nullptr ||
      *endpoint_end != '\0' || parsed_node == 0 || parsed_endpoint == 0 || parsed_endpoint > 0xffff)
    return false;
  node_id = parsed_node;
  endpoint_id = static_cast<std::uint16_t>(parsed_endpoint);
  return devices_.contains(node_id, endpoint_id);
}

} // namespace core_pilot
