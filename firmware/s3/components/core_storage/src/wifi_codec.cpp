#include "wifi_codec.hpp"

#include <cstring>

namespace core_storage {
namespace {
bool utf8(const std::uint8_t* bytes, std::size_t size) {
  for (std::size_t i = 0; i < size;) {
    const std::uint8_t lead = bytes[i++];
    if (lead < 0x80)
      continue;
    unsigned more = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if (lead >= 0xc2 && lead <= 0xdf) {
      more = 1;
      value = lead & 31;
      minimum = 0x80;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      more = 2;
      value = lead & 15;
      minimum = 0x800;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      more = 3;
      value = lead & 7;
      minimum = 0x10000;
    } else {
      return false;
    }
    if (more > size - i)
      return false;
    for (unsigned j = 0; j < more; ++j) {
      const std::uint8_t b = bytes[i++];
      if ((b & 0xc0) != 0x80)
        return false;
      value = (value << 6) | (b & 63);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
      return false;
  }
  return true;
}

bool valid_network(const WifiNetwork& network) {
  return network.ssid != nullptr && network.ssid_len != 0 && network.ssid_len <= 32 &&
         utf8(network.ssid, network.ssid_len) &&
         (network.password_len == 0 ||
          (network.password != nullptr && network.password_len >= 8 && network.password_len <= 63 &&
           utf8(network.password, network.password_len)));
}

bool valid_config(const WifiConfig& config) {
  if (!valid_network({config.ssid, config.ssid_len, config.password, config.password_len}))
    return false;
  if (config.hostname == nullptr)
    return config.hostname_len == 0;
  if (config.hostname_len == 0 || config.hostname_len > 63)
    return false;
  for (std::size_t i = 0; i < config.hostname_len; ++i) {
    const std::uint8_t byte = config.hostname[i];
    if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
          (byte >= '0' && byte <= '9') || byte == '-'))
      return false;
  }
  return config.hostname[0] != '-' && config.hostname[config.hostname_len - 1] != '-';
}

bool field(const std::uint8_t* input, std::size_t length, std::size_t& position,
           const std::uint8_t*& data, std::size_t& size, std::size_t limit) {
  if (position >= length || input[position] > limit)
    return false;
  size = input[position++];
  if (size > length - position)
    return false;
  data = input + position;
  position += size;
  return true;
}
} // namespace

core_security::Status encode_wifi(const WifiConfig& config, std::uint8_t* out, std::size_t capacity,
                                  std::size_t& written) {
  written = 0;
  if (out == nullptr || !valid_config(config))
    return core_security::Status::Invalid;
  const std::size_t required = 1 + config.ssid_len + 1 + config.password_len + 1 +
                               (config.hostname != nullptr ? 1 + config.hostname_len : 0);
  if (capacity < required)
    return core_security::Status::BufferTooSmall;
  std::size_t position = 0;
  out[position++] = static_cast<std::uint8_t>(config.ssid_len);
  std::memcpy(out + position, config.ssid, config.ssid_len);
  position += config.ssid_len;
  out[position++] = static_cast<std::uint8_t>(config.password_len);
  if (config.password_len != 0)
    std::memcpy(out + position, config.password, config.password_len);
  position += config.password_len;
  out[position++] = config.hostname != nullptr ? 1 : 0;
  if (config.hostname != nullptr) {
    out[position++] = static_cast<std::uint8_t>(config.hostname_len);
    std::memcpy(out + position, config.hostname, config.hostname_len);
    position += config.hostname_len;
  }
  written = position;
  return core_security::Status::Ok;
}

core_security::Status decode_wifi(std::uint16_t schema, const std::uint8_t* input,
                                  std::size_t length, WifiConfig& config) {
  config = {};
  if (schema != kWifiSchema1)
    return core_security::Status::UnsupportedFormat;
  if (input == nullptr || length > kMaxWifiSchema1Payload)
    return core_security::Status::Invalid;
  WifiConfig result{};
  std::size_t position = 0;
  if (!field(input, length, position, result.ssid, result.ssid_len, 32) ||
      !field(input, length, position, result.password, result.password_len, 63) ||
      position >= length)
    return core_security::Status::Invalid;
  const std::uint8_t option = input[position++];
  if (option == 1) {
    if (!field(input, length, position, result.hostname, result.hostname_len, 63))
      return core_security::Status::Invalid;
  } else if (option != 0) {
    return core_security::Status::Invalid;
  }
  if (position != length || !valid_config(result))
    return core_security::Status::Invalid;
  config = result;
  return core_security::Status::Ok;
}

core_security::Status encode_wifi_networks(const WifiNetworks& config, std::uint8_t* out,
                                           std::size_t capacity, std::size_t& written) {
  written = 0;
  if (out == nullptr || config.count > kMaxSavedWifiNetworks)
    return core_security::Status::Invalid;
  std::size_t required = 1;
  for (std::size_t i = 0; i < config.count; ++i) {
    if (!valid_network(config.networks[i]))
      return core_security::Status::Invalid;
    for (std::size_t previous = 0; previous < i; ++previous) {
      if (config.networks[previous].ssid_len == config.networks[i].ssid_len &&
          std::memcmp(config.networks[previous].ssid, config.networks[i].ssid,
                      config.networks[i].ssid_len) == 0)
        return core_security::Status::Invalid;
    }
    required += 2 + config.networks[i].ssid_len + config.networks[i].password_len;
  }
  if (capacity < required)
    return core_security::Status::BufferTooSmall;
  std::size_t position = 0;
  out[position++] = static_cast<std::uint8_t>(config.count);
  for (std::size_t i = 0; i < config.count; ++i) {
    const auto& network = config.networks[i];
    out[position++] = static_cast<std::uint8_t>(network.ssid_len);
    std::memcpy(out + position, network.ssid, network.ssid_len);
    position += network.ssid_len;
    out[position++] = static_cast<std::uint8_t>(network.password_len);
    if (network.password_len != 0)
      std::memcpy(out + position, network.password, network.password_len);
    position += network.password_len;
  }
  written = position;
  return core_security::Status::Ok;
}

core_security::Status decode_wifi_networks(std::uint16_t schema, const std::uint8_t* input,
                                           std::size_t length, WifiNetworks& config) {
  config = {};
  if (schema != kWifiSchema)
    return core_security::Status::UnsupportedFormat;
  if (input == nullptr || length == 0 || length > kMaxWifiPayload ||
      input[0] > kMaxSavedWifiNetworks)
    return core_security::Status::Invalid;
  WifiNetworks result{};
  result.count = input[0];
  std::size_t position = 1;
  for (std::size_t i = 0; i < result.count; ++i) {
    auto& network = result.networks[i];
    if (!field(input, length, position, network.ssid, network.ssid_len, 32) ||
        !field(input, length, position, network.password, network.password_len, 63) ||
        !valid_network(network))
      return core_security::Status::Invalid;
    for (std::size_t previous = 0; previous < i; ++previous) {
      if (result.networks[previous].ssid_len == network.ssid_len &&
          std::memcmp(result.networks[previous].ssid, network.ssid, network.ssid_len) == 0)
        return core_security::Status::Invalid;
    }
  }
  if (position != length)
    return core_security::Status::Invalid;
  config = result;
  return core_security::Status::Ok;
}

} // namespace core_storage
