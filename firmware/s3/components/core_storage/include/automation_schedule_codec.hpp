#pragma once

#include "core_security.hpp"

#include <cstddef>
#include <cstdint>

namespace core_storage {

constexpr std::uint16_t kAutomationScheduleCheckpointsSchema = 1;
constexpr std::size_t kMaxAutomationScheduleCheckpoints = 32;
constexpr std::size_t kMaxAutomationIdBytes = 48;
constexpr std::size_t kMaxAutomationTriggerBytes = 96;
constexpr std::size_t kMaxAutomationScheduleCheckpointsPayload =
    1 + kMaxAutomationScheduleCheckpoints *
            (1 + kMaxAutomationIdBytes + 1 + kMaxAutomationTriggerBytes + sizeof(std::uint64_t));

struct AutomationScheduleCheckpoint final {
  char automation_id[kMaxAutomationIdBytes + 1];
  char exact_trigger[kMaxAutomationTriggerBytes + 1];
  std::uint64_t last_utc_occurrence_minute;
};

struct AutomationScheduleCheckpoints final {
  AutomationScheduleCheckpoint entries[kMaxAutomationScheduleCheckpoints]{};
  std::size_t count{0};
};

core_security::Status
encode_automation_schedule_checkpoints(const AutomationScheduleCheckpoints& checkpoints,
                                       std::uint8_t* output, std::size_t capacity,
                                       std::size_t& written);
core_security::Status
decode_automation_schedule_checkpoints(std::uint16_t schema, const std::uint8_t* input,
                                       std::size_t length,
                                       AutomationScheduleCheckpoints& checkpoints);

} // namespace core_storage
