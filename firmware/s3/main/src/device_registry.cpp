#include "device_registry.hpp"

#include "mbedtls/platform_util.h"

#include <cstring>

namespace core_pilot {
namespace {

constexpr char kPath[] = "/core-pilot/matter/devices.enc";
constexpr char kLegacyPath[] = "/core-pilot/matter/devices-legacy.enc";
constexpr std::size_t kEncryptedScratchBytes = core_storage::kMaxMatterDevicesPayload + 64;
constexpr core_security::RecordSpec recordSpec(std::uint64_t generation) {
  return {kPath,
          sizeof(kPath) - 1,
          core_security::Namespace::Matter,
          core_security::RecordType::MatterDevices,
          core_storage::kMatterDevicesSchema,
          generation};
}

class SemaphoreLock final {
public:
  explicit SemaphoreLock(SemaphoreHandle_t mutex)
      : mutex_(mutex),
        held_(mutex != nullptr && xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {}
  ~SemaphoreLock() {
    if (held_)
      xSemaphoreGive(mutex_);
  }
  bool held() const {
    return held_;
  }

private:
  SemaphoreHandle_t mutex_{};
  bool held_{};
};

} // namespace

DeviceRegistry::DeviceRegistry(core_security::StorageService& storage,
                               core_security::RandomSource& random)
    : storage_(storage), random_(random),
      mutation_mutex_(xSemaphoreCreateMutexStatic(&mutation_mutex_storage_)) {}

core_security::Status DeviceRegistry::load() {
  std::uint8_t encrypted[kEncryptedScratchBytes]{};
  std::uint8_t plaintext[core_storage::kMaxMatterDevicesPayload]{};
  std::size_t size = 0;
  core_security::Metadata metadata{};
  const auto result = storage_.get(recordSpec(0), encrypted, sizeof(encrypted), plaintext,
                                   sizeof(plaintext), size, metadata);
  if (result == core_security::Status::NotFound) {
    return core_security::Status::Ok;
  }
  if (result != core_security::Status::Ok) {
    writable_ = false;
    return result;
  }
  core_storage::MatterDevices decoded{};
  const auto decode =
      core_storage::decode_matter_devices(metadata.schema_version, plaintext, size, decoded);
  mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
  if (decode != core_security::Status::Ok) {
    writable_ = false;
    return decode;
  }
  for (std::size_t index = 0; index < decoded.count; ++index)
    decoded.entries[index].reachable = false;
  taskENTER_CRITICAL(&lock_);
  devices_ = decoded;
  generation_ = metadata.generation;
  taskEXIT_CRITICAL(&lock_);
  return core_security::Status::Ok;
}

core_security::Status DeviceRegistry::add(std::uint64_t node_id, std::uint16_t endpoint_id,
                                          std::uint32_t capabilities) {
  if (node_id == 0 || endpoint_id == 0) {
    return core_security::Status::Invalid;
  }
  if (!writable_) {
    return core_security::Status::UnsupportedFormat;
  }
  SemaphoreLock mutation(mutation_mutex_);
  if (!mutation.held())
    return core_security::Status::IoError;
  auto candidate = snapshot();
  for (std::size_t index = 0; index < candidate.count; ++index) {
    if (candidate.entries[index].node_id == node_id &&
        candidate.entries[index].endpoint_id == endpoint_id) {
      candidate.entries[index].capabilities = capabilities;
      candidate.entries[index].reachable = true;
      return persist(candidate);
    }
  }
  if (candidate.count == core_storage::kMaxMatterDevices)
    return core_security::Status::TooLarge;
  candidate.entries[candidate.count++] = {node_id, endpoint_id, capabilities, true, {}};
  return persist(candidate);
}

core_security::Status DeviceRegistry::replaceNode(std::uint64_t node_id,
                                                  const core_storage::MatterDevice* endpoints,
                                                  std::size_t count) {
  if (node_id == 0 || (count != 0 && endpoints == nullptr) ||
      count > core_storage::kMaxMatterDevices || !writable_) {
    return core_security::Status::Invalid;
  }
  SemaphoreLock mutation(mutation_mutex_);
  if (!mutation.held())
    return core_security::Status::IoError;
  const auto current = snapshot();
  core_storage::MatterDevices updated{};
  for (std::size_t index = 0; index < current.count; ++index) {
    if (current.entries[index].node_id != node_id)
      updated.entries[updated.count++] = current.entries[index];
  }
  if (updated.count + count > core_storage::kMaxMatterDevices)
    return core_security::Status::TooLarge;
  for (std::size_t index = 0; index < count; ++index) {
    if (endpoints[index].node_id != node_id || endpoints[index].endpoint_id == 0)
      return core_security::Status::Invalid;
    auto endpoint = endpoints[index];
    for (std::size_t current_index = 0; current_index < current.count; ++current_index) {
      const auto& existing = current.entries[current_index];
      if (existing.node_id == node_id && existing.endpoint_id == endpoint.endpoint_id) {
        std::memcpy(endpoint.name, existing.name, sizeof endpoint.name);
        break;
      }
    }
    updated.entries[updated.count++] = endpoint;
  }

  bool persistent_change = current.count != updated.count;
  for (std::size_t index = 0; !persistent_change && index < current.count; ++index) {
    const auto& before = current.entries[index];
    const auto& after = updated.entries[index];
    persistent_change =
        before.node_id != after.node_id || before.endpoint_id != after.endpoint_id ||
        before.capabilities != after.capabilities || std::strcmp(before.name, after.name) != 0;
  }
  if (!persistent_change) {
    taskENTER_CRITICAL(&lock_);
    devices_ = updated;
    taskEXIT_CRITICAL(&lock_);
    return core_security::Status::Ok;
  }
  return persist(updated);
}

core_security::Status DeviceRegistry::removeNode(std::uint64_t node_id) {
  if (node_id == 0 || !writable_)
    return core_security::Status::Invalid;
  return replaceNode(node_id, nullptr, 0);
}

core_security::Status DeviceRegistry::rename(std::uint64_t node_id, std::uint16_t endpoint_id,
                                             const char* name) {
  if (node_id == 0 || endpoint_id == 0 || name == nullptr || !writable_)
    return core_security::Status::Invalid;
  const std::size_t name_length = strnlen(name, core_storage::kMaxMatterDeviceNameBytes + 1);
  if (name_length == 0 || name_length > core_storage::kMaxMatterDeviceNameBytes)
    return core_security::Status::Invalid;
  SemaphoreLock mutation(mutation_mutex_);
  if (!mutation.held())
    return core_security::Status::IoError;
  auto candidate = snapshot();
  for (std::size_t index = 0; index < candidate.count; ++index) {
    auto& device = candidate.entries[index];
    if (device.node_id == node_id && device.endpoint_id == endpoint_id) {
      std::memcpy(device.name, name, name_length);
      device.name[name_length] = '\0';
      return persist(candidate);
    }
  }
  return core_security::Status::NotFound;
}

bool DeviceRegistry::contains(std::uint64_t node_id, std::uint16_t endpoint_id) const {
  taskENTER_CRITICAL(&lock_);
  bool found = false;
  for (std::size_t index = 0; index < devices_.count; ++index) {
    if (devices_.entries[index].node_id == node_id &&
        devices_.entries[index].endpoint_id == endpoint_id) {
      found = true;
      break;
    }
  }
  taskEXIT_CRITICAL(&lock_);
  return found;
}

core_security::Status DeviceRegistry::resetUnsupported() {
  SemaphoreLock mutation(mutation_mutex_);
  if (!mutation.held())
    return core_security::Status::IoError;
  if (writable_.load())
    return core_security::Status::Invalid;
  auto result = storage_.archive(kPath, sizeof(kPath) - 1, kLegacyPath, sizeof(kLegacyPath) - 1);
  if (result != core_security::Status::Ok)
    return result;
  taskENTER_CRITICAL(&lock_);
  generation_ = 0;
  taskEXIT_CRITICAL(&lock_);
  result = persist({});
  writable_.store(result == core_security::Status::Ok);
  return result;
}

core_storage::MatterDevices DeviceRegistry::snapshot() const {
  taskENTER_CRITICAL(&lock_);
  const auto copy = devices_;
  taskEXIT_CRITICAL(&lock_);
  return copy;
}

core_security::Status DeviceRegistry::persist(const core_storage::MatterDevices& candidate) {
  std::uint64_t generation = 0;
  taskENTER_CRITICAL(&lock_);
  generation = generation_ + 1;
  taskEXIT_CRITICAL(&lock_);
  std::uint8_t plaintext[core_storage::kMaxMatterDevicesPayload]{};
  std::uint8_t encrypted[kEncryptedScratchBytes]{};
  std::size_t written = 0;
  auto result =
      core_storage::encode_matter_devices(candidate, plaintext, sizeof(plaintext), written);
  if (result == core_security::Status::Ok)
    result = storage_.put(recordSpec(generation), plaintext, written, random_, encrypted,
                          sizeof(encrypted));
  mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
  if (result == core_security::Status::Ok) {
    taskENTER_CRITICAL(&lock_);
    devices_ = candidate;
    generation_ = generation;
    taskEXIT_CRITICAL(&lock_);
  }
  return result;
}

} // namespace core_pilot
