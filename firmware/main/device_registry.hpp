#pragma once

#include "core_security.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "matter_devices_codec.hpp"

#include <atomic>

namespace core_pilot {

class DeviceRegistry final {
public:
  DeviceRegistry(core_security::StorageService& storage, core_security::RandomSource& random);

  core_security::Status load();
  core_security::Status add(std::uint64_t node_id, std::uint16_t endpoint_id,
                            std::uint32_t capabilities = 0);
  core_security::Status replaceNode(std::uint64_t node_id,
                                    const core_storage::MatterDevice* endpoints, std::size_t count);
  core_security::Status removeNode(std::uint64_t node_id);
  bool contains(std::uint64_t node_id, std::uint16_t endpoint_id) const;
  bool writable() const {
    return writable_.load();
  }
  core_security::Status resetUnsupported();
  core_storage::MatterDevices snapshot() const;

private:
  core_security::Status persist(const core_storage::MatterDevices& candidate);

  core_security::StorageService& storage_;
  core_security::RandomSource& random_;
  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  StaticSemaphore_t mutation_mutex_storage_{};
  SemaphoreHandle_t mutation_mutex_{};
  core_storage::MatterDevices devices_{};
  std::uint64_t generation_{0};
  std::atomic_bool writable_{true};
};

} // namespace core_pilot
