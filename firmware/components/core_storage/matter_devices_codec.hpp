#pragma once

#include "core_security.hpp"

#include <cstddef>
#include <cstdint>

namespace core_storage {

constexpr std::uint16_t kMatterDevicesSchema = 2;
constexpr std::size_t kMaxMatterDevices = 16;
constexpr std::size_t kMaxMatterDevicesPayload = 1 + kMaxMatterDevices * 15;

struct MatterDevice final {
  std::uint64_t node_id;
  std::uint16_t endpoint_id;
  std::uint32_t capabilities;
  bool reachable;
};

struct MatterDevices final {
  MatterDevice entries[kMaxMatterDevices]{};
  std::size_t count{0};
};

core_security::Status encode_matter_devices(const MatterDevices& devices, std::uint8_t* output,
                                            std::size_t capacity, std::size_t& written);
core_security::Status decode_matter_devices(std::uint16_t schema, const std::uint8_t* input,
                                            std::size_t length, MatterDevices& devices);

} // namespace core_storage
