#include "wifi_codec.hpp"

#include <cstring>

namespace core_storage {
namespace {
bool utf8(const uint8_t* bytes, size_t size) {
  for (size_t i = 0; i < size;) {
    const uint8_t lead = bytes[i++];
    if (lead < 0x80)
      continue;
    unsigned more = 0;
    uint32_t value = 0, minimum = 0;
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
    } else
      return false;
    if (more > size - i)
      return false;
    for (unsigned j = 0; j < more; ++j) {
      const uint8_t b = bytes[i++];
      if ((b & 0xc0) != 0x80)
        return false;
      value = (value << 6) | (b & 63);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
      return false;
  }
  return true;
}
bool valid(const WifiConfig& v) {
  if (!v.ssid || v.ssid_len == 0 || v.ssid_len > 32 || !utf8(v.ssid, v.ssid_len) ||
      (v.password_len && !v.password) || v.password_len > 63 ||
      (v.password_len && v.password_len < 8) || !utf8(v.password, v.password_len))
    return false;
  if (!v.hostname)
    return v.hostname_len == 0;
  if (v.hostname_len == 0 || v.hostname_len > 63)
    return false;
  for (size_t i = 0; i < v.hostname_len; ++i) {
    const uint8_t b = v.hostname[i];
    if (!((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '-'))
      return false;
  }
  return v.hostname[0] != '-' && v.hostname[v.hostname_len - 1] != '-';
}
bool field(const uint8_t* input, size_t length, size_t& pos, const uint8_t*& data, size_t& size,
           size_t limit) {
  // All current field bounds fit a single canonical postcard varint byte.
  if (pos >= length || input[pos] > limit)
    return false;
  size = input[pos++];
  if (size > length - pos)
    return false;
  data = input + pos;
  pos += size;
  return true;
}
} // namespace

core_security::Status encode_wifi(const WifiConfig& config, uint8_t* out, size_t capacity,
                                  size_t& written) {
  written = 0;
  if (!out || !valid(config))
    return core_security::Status::Invalid;
  const size_t required = 1 + config.ssid_len + 1 + config.password_len + 1 +
                          (config.hostname ? 1 + config.hostname_len : 0);
  if (capacity < required)
    return core_security::Status::BufferTooSmall;
  size_t pos = 0;
  out[pos++] = static_cast<uint8_t>(config.ssid_len);
  std::memcpy(out + pos, config.ssid, config.ssid_len);
  pos += config.ssid_len;
  out[pos++] = static_cast<uint8_t>(config.password_len);
  if (config.password_len)
    std::memcpy(out + pos, config.password, config.password_len);
  pos += config.password_len;
  out[pos++] = config.hostname ? 1 : 0;
  if (config.hostname) {
    out[pos++] = static_cast<uint8_t>(config.hostname_len);
    std::memcpy(out + pos, config.hostname, config.hostname_len);
    pos += config.hostname_len;
  }
  written = pos;
  return core_security::Status::Ok;
}

core_security::Status decode_wifi(uint16_t schema, const uint8_t* input, size_t length,
                                  WifiConfig& config) {
  config = {};
  if (schema != kWifiSchema)
    return core_security::Status::UnsupportedFormat;
  if (!input || length > kMaxWifiPayload)
    return core_security::Status::Invalid;
  WifiConfig result{};
  size_t pos = 0;
  if (!field(input, length, pos, result.ssid, result.ssid_len, 32) ||
      !field(input, length, pos, result.password, result.password_len, 63) || pos >= length)
    return core_security::Status::Invalid;
  const uint8_t option = input[pos++];
  if (option == 1) {
    if (!field(input, length, pos, result.hostname, result.hostname_len, 63))
      return core_security::Status::Invalid;
  } else if (option != 0)
    return core_security::Status::Invalid;
  if (pos != length || !valid(result))
    return core_security::Status::Invalid;
  config = result;
  return core_security::Status::Ok;
}
} // namespace core_storage
