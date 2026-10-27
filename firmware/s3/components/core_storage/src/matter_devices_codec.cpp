#include "matter_devices_codec.hpp"

#include <cstring>

namespace core_storage {
namespace {

void writeU64(std::uint8_t* output, std::uint64_t value) {
  for (unsigned index = 0; index < 8; ++index) {
    output[index] = static_cast<std::uint8_t>(value >> (index * 8));
  }
}

void writeU32(std::uint8_t* output, std::uint32_t value) {
  for (unsigned index = 0; index < 4; ++index) {
    output[index] = static_cast<std::uint8_t>(value >> (index * 8));
  }
}

std::uint64_t readU64(const std::uint8_t* input) {
  std::uint64_t value = 0;
  for (unsigned index = 0; index < 8; ++index) {
    value |= static_cast<std::uint64_t>(input[index]) << (index * 8);
  }
  return value;
}

std::uint32_t readU32(const std::uint8_t* input) {
  std::uint32_t value = 0;
  for (unsigned index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(input[index]) << (index * 8);
  }
  return value;
}

} // namespace

core_security::Status encode_matter_devices(const MatterDevices& devices, std::uint8_t* output,
                                            std::size_t capacity, std::size_t& written) {
  written = 0;
  if (output == nullptr || devices.count > kMaxMatterDevices) {
    return core_security::Status::Invalid;
  }
  std::size_t required = 1 + devices.count * 16;
  for (std::size_t index = 0; index < devices.count; ++index) {
    const auto& device = devices.entries[index];
    if (device.node_id == 0 || device.endpoint_id == 0)
      return core_security::Status::Invalid;
    const std::size_t name_length = strnlen(device.name, sizeof device.name);
    if (name_length > kMaxMatterDeviceNameBytes)
      return core_security::Status::Invalid;
    required += name_length;
  }
  if (capacity < required)
    return core_security::Status::BufferTooSmall;
  output[0] = static_cast<std::uint8_t>(devices.count);
  std::size_t position = 1;
  for (std::size_t index = 0; index < devices.count; ++index) {
    const auto& device = devices.entries[index];
    writeU64(output + position, device.node_id);
    position += 8;
    output[position++] = static_cast<std::uint8_t>(device.endpoint_id);
    output[position++] = static_cast<std::uint8_t>(device.endpoint_id >> 8);
    writeU32(output + position, device.capabilities);
    position += 4;
    output[position++] = device.reachable ? 1 : 0;
    const std::size_t name_length = strnlen(device.name, sizeof device.name);
    output[position++] = static_cast<std::uint8_t>(name_length);
    std::memcpy(output + position, device.name, name_length);
    position += name_length;
  }
  written = position;
  return core_security::Status::Ok;
}

core_security::Status decode_matter_devices(std::uint16_t schema, const std::uint8_t* input,
                                            std::size_t length, MatterDevices& devices) {
  devices = {};
  if (schema != 1 && schema != 2 && schema != kMatterDevicesSchema) {
    return core_security::Status::UnsupportedFormat;
  }
  if (input == nullptr || length == 0 || length > kMaxMatterDevicesPayload) {
    return core_security::Status::Invalid;
  }
  const std::size_t count = input[0];
  if (count > kMaxMatterDevices)
    return core_security::Status::Invalid;
  if (schema < kMatterDevicesSchema) {
    const std::size_t entry_size = schema == 1 ? 11 : 15;
    if (length != 1 + count * entry_size)
      return core_security::Status::Invalid;
  }
  std::size_t position = 1;
  for (std::size_t index = 0; index < count; ++index) {
    if (schema == kMatterDevicesSchema && length - position < 16)
      return core_security::Status::Invalid;
    auto& device = devices.entries[index];
    device.node_id = readU64(input + position);
    position += 8;
    device.endpoint_id = static_cast<std::uint16_t>(input[position]) |
                         static_cast<std::uint16_t>(input[position + 1]) << 8;
    position += 2;
    if (schema >= 2) {
      device.capabilities = readU32(input + position);
      position += 4;
    }
    if (position >= length || input[position] > 1 || device.node_id == 0 ||
        device.endpoint_id == 0) {
      return core_security::Status::Invalid;
    }
    device.reachable = input[position++] == 1;
    if (schema == kMatterDevicesSchema) {
      if (position >= length)
        return core_security::Status::Invalid;
      const std::size_t name_length = input[position++];
      if (name_length > kMaxMatterDeviceNameBytes || name_length > length - position ||
          std::memchr(input + position, '\0', name_length) != nullptr)
        return core_security::Status::Invalid;
      std::memcpy(device.name, input + position, name_length);
      device.name[name_length] = '\0';
      position += name_length;
    }
  }
  if (position != length)
    return core_security::Status::Invalid;
  devices.count = count;
  return core_security::Status::Ok;
}

} // namespace core_storage
