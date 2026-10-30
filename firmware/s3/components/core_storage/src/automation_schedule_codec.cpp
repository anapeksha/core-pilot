#include "automation_schedule_codec.hpp"

#include "core_domain.hpp"

#include <cstring>

namespace core_storage {
namespace {

void writeU64(std::uint8_t* output, std::uint64_t value) {
  for (unsigned index = 0; index < sizeof value; ++index)
    output[index] = static_cast<std::uint8_t>(value >> (index * 8));
}

std::uint64_t readU64(const std::uint8_t* input) {
  std::uint64_t value = 0;
  for (unsigned index = 0; index < sizeof value; ++index)
    value |= static_cast<std::uint64_t>(input[index]) << (index * 8);
  return value;
}

bool validTrigger(const char* trigger) {
  core_domain::TriggerSpec parsed{};
  return core_domain::parse_trigger_spec(trigger, parsed) &&
         parsed.kind != core_domain::TriggerSpec::Kind::manual;
}

bool uniqueId(const AutomationScheduleCheckpoints& checkpoints, std::size_t current) {
  for (std::size_t index = 0; index < current; ++index)
    if (std::strcmp(checkpoints.entries[index].automation_id,
                    checkpoints.entries[current].automation_id) == 0)
      return false;
  return true;
}

} // namespace

core_security::Status
encode_automation_schedule_checkpoints(const AutomationScheduleCheckpoints& checkpoints,
                                       std::uint8_t* output, std::size_t capacity,
                                       std::size_t& written) {
  written = 0;
  if (output == nullptr || checkpoints.count > kMaxAutomationScheduleCheckpoints)
    return core_security::Status::Invalid;

  std::size_t required = 1;
  for (std::size_t index = 0; index < checkpoints.count; ++index) {
    const auto& checkpoint = checkpoints.entries[index];
    const std::size_t id_length =
        strnlen(checkpoint.automation_id, sizeof checkpoint.automation_id);
    const std::size_t trigger_length =
        strnlen(checkpoint.exact_trigger, sizeof checkpoint.exact_trigger);
    if (id_length == 0 || id_length > kMaxAutomationIdBytes || trigger_length == 0 ||
        trigger_length > kMaxAutomationTriggerBytes || !validTrigger(checkpoint.exact_trigger) ||
        !uniqueId(checkpoints, index))
      return core_security::Status::Invalid;
    required += 1 + id_length + 1 + trigger_length + sizeof(std::uint64_t);
  }
  if (capacity < required)
    return core_security::Status::BufferTooSmall;

  output[0] = static_cast<std::uint8_t>(checkpoints.count);
  std::size_t position = 1;
  for (std::size_t index = 0; index < checkpoints.count; ++index) {
    const auto& checkpoint = checkpoints.entries[index];
    const std::size_t id_length = std::strlen(checkpoint.automation_id);
    const std::size_t trigger_length = std::strlen(checkpoint.exact_trigger);
    output[position++] = static_cast<std::uint8_t>(id_length);
    std::memcpy(output + position, checkpoint.automation_id, id_length);
    position += id_length;
    output[position++] = static_cast<std::uint8_t>(trigger_length);
    std::memcpy(output + position, checkpoint.exact_trigger, trigger_length);
    position += trigger_length;
    writeU64(output + position, checkpoint.last_utc_occurrence_minute);
    position += sizeof(std::uint64_t);
  }
  written = position;
  return core_security::Status::Ok;
}

core_security::Status
decode_automation_schedule_checkpoints(std::uint16_t schema, const std::uint8_t* input,
                                       std::size_t length,
                                       AutomationScheduleCheckpoints& checkpoints) {
  checkpoints = {};
  if (schema != kAutomationScheduleCheckpointsSchema)
    return core_security::Status::UnsupportedFormat;
  if (input == nullptr || length == 0 || length > kMaxAutomationScheduleCheckpointsPayload)
    return core_security::Status::Invalid;

  const std::size_t count = input[0];
  if (count > kMaxAutomationScheduleCheckpoints)
    return core_security::Status::Invalid;
  std::size_t position = 1;
  for (std::size_t index = 0; index < count; ++index) {
    if (position >= length)
      return core_security::Status::Invalid;
    const std::size_t id_length = input[position++];
    if (id_length == 0 || id_length > kMaxAutomationIdBytes || id_length > length - position ||
        std::memchr(input + position, '\0', id_length) != nullptr)
      return core_security::Status::Invalid;
    auto& checkpoint = checkpoints.entries[index];
    std::memcpy(checkpoint.automation_id, input + position, id_length);
    checkpoint.automation_id[id_length] = '\0';
    position += id_length;

    if (position >= length)
      return core_security::Status::Invalid;
    const std::size_t trigger_length = input[position++];
    if (trigger_length == 0 || trigger_length > kMaxAutomationTriggerBytes ||
        trigger_length > length - position ||
        std::memchr(input + position, '\0', trigger_length) != nullptr)
      return core_security::Status::Invalid;
    std::memcpy(checkpoint.exact_trigger, input + position, trigger_length);
    checkpoint.exact_trigger[trigger_length] = '\0';
    position += trigger_length;

    if (length - position < sizeof(std::uint64_t) || !validTrigger(checkpoint.exact_trigger) ||
        !uniqueId(checkpoints, index))
      return core_security::Status::Invalid;
    checkpoint.last_utc_occurrence_minute = readU64(input + position);
    position += sizeof(std::uint64_t);
  }
  if (position != length)
    return core_security::Status::Invalid;
  checkpoints.count = count;
  return core_security::Status::Ok;
}

} // namespace core_storage
