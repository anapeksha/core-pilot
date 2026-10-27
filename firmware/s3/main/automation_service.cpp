#include "automation_service.hpp"

#include "core_activity.hpp"
#include "core_matter.hpp"
#include "device_registry.hpp"
#include "model_repository.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace core_pilot {

AutomationService::AutomationService(ModelRepository& models, DeviceRegistry& devices,
                                     CoreMatter& matter, ActivityService& activity)
    : models_(models), devices_(devices), matter_(matter), activity_(activity) {}

esp_err_t AutomationService::start() {
  if (task_ != nullptr)
    return ESP_OK;
  queue_ = xQueueCreateStatic(kQueueDepth, sizeof(Work), queue_storage_, &queue_buffer_);
  if (queue_ == nullptr)
    return ESP_ERR_NO_MEM;
  task_ =
      xTaskCreateStaticPinnedToCore(taskEntry, "automation", kTaskStackBytes / sizeof(StackType_t),
                                    this, 3, task_stack_, &task_buffer_, 0);
  return task_ == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
}

AutomationService::SubmitResult AutomationService::submit(const char* id) {
  if (id == nullptr || queue_ == nullptr || task_ == nullptr)
    return SubmitResult::not_found;
  Work work{};
  if (!models_.find(core_domain::ModelKind::automation, id, work.automation))
    return SubmitResult::not_found;
  if (!work.automation.enabled)
    return SubmitResult::disabled;
  core_domain::TriggerSpec trigger{};
  if (!core_domain::parse_trigger_spec(work.automation.trigger, trigger) ||
      trigger.kind != core_domain::TriggerSpec::Kind::manual)
    return SubmitResult::not_manual;

  for (std::size_t index = 0; index < work.automation.detail_count; ++index) {
    core_domain::ActionSpec action{};
    if (!core_domain::parse_action_spec(work.automation.details[index], action) ||
        (action.kind != core_domain::ActionSpec::Kind::on &&
         action.kind != core_domain::ActionSpec::Kind::delay))
      return SubmitResult::unsupported_action;
    if (action.kind == core_domain::ActionSpec::Kind::on) {
      std::uint64_t node_id = 0;
      std::uint16_t endpoint_id = 0;
      if (!resolveTarget(action.target, node_id, endpoint_id))
        return SubmitResult::target_unavailable;
    }
  }
  if (xQueueSend(queue_, &work, 0) != pdTRUE)
    return SubmitResult::queue_full;
  activity_.append("automation_queued", "A manual automation was queued.");
  return SubmitResult::queued;
}

void AutomationService::taskEntry(void* context) {
  static_cast<AutomationService*>(context)->taskLoop();
}

void AutomationService::taskLoop() {
  Work work{};
  while (true) {
    if (xQueueReceive(queue_, &work, portMAX_DELAY) == pdTRUE)
      run(work);
  }
}

void AutomationService::run(const Work& work) {
  for (std::size_t index = 0; index < work.automation.detail_count; ++index) {
    core_domain::ActionSpec action{};
    if (!core_domain::parse_action_spec(work.automation.details[index], action)) {
      activity_.append("automation_failed", "A stored automation action was invalid.");
      return;
    }
    if (action.kind == core_domain::ActionSpec::Kind::delay) {
      vTaskDelay(pdMS_TO_TICKS(action.value));
      continue;
    }
    std::uint64_t node_id = 0;
    std::uint16_t endpoint_id = 0;
    std::uint32_t operation_id = 0;
    if (!resolveTarget(action.target, node_id, endpoint_id) ||
        matter_.setOnOff(node_id, endpoint_id, action.value != 0, operation_id) != ESP_OK) {
      activity_.append("automation_failed", "A Matter automation command was rejected.");
      return;
    }
    CoreMatter::TerminalCommand terminal{};
    if (matter_.waitForTerminalCommand(operation_id, kMatterCommandTimeout, terminal) != ESP_OK ||
        terminal.status.state != CoreMatter::CommandState::complete) {
      activity_.append("automation_failed", "A Matter automation command did not complete.");
      return;
    }
  }
  activity_.append("automation_complete", "A manual automation completed successfully.");
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
