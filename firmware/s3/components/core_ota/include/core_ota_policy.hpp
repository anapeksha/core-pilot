#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace core_pilot::ota_policy {

struct Identifier final {
  const char* data{};
  std::size_t size{};
};

struct Version final {
  Identifier core[3]{};
  Identifier prerelease{};
};

enum class ChunkError : std::uint8_t {
  ok,
  invalid_argument,
  chunk_too_large,
  offset_mismatch,
  image_too_large,
  final_mismatch,
};

constexpr std::uint8_t hex_value(char value) {
  if (value >= '0' && value <= '9')
    return static_cast<std::uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<std::uint8_t>(value - 'a' + 10);
  if (value >= 'A' && value <= 'F')
    return static_cast<std::uint8_t>(value - 'A' + 10);
  return 0xff;
}

inline bool parse_digest(const char* hex, std::uint8_t* digest, std::size_t digest_size) {
  if (hex == nullptr || digest == nullptr)
    return false;
  const std::size_t expected_size = digest_size * 2;
  std::size_t hex_size = 0;
  while (hex_size <= expected_size && hex[hex_size] != '\0')
    ++hex_size;
  if (hex_size != expected_size)
    return false;
  for (std::size_t index = 0; index < digest_size; ++index) {
    const std::uint8_t high = hex_value(hex[index * 2]);
    const std::uint8_t low = hex_value(hex[index * 2 + 1]);
    if (high == 0xff || low == 0xff)
      return false;
    digest[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return hex[digest_size * 2] == '\0';
}

inline bool valid_text_version(const char* version, std::size_t maximum_size) {
  if (version == nullptr)
    return false;
  bool nonspace = false;
  for (std::size_t index = 0; index <= maximum_size; ++index) {
    const unsigned char value = static_cast<unsigned char>(version[index]);
    if (value == 0)
      return index != 0 && nonspace;
    if (index == maximum_size || value < 0x20 || value > 0x7e)
      return false;
    if (value != ' ')
      nonspace = true;
  }
  return false;
}

inline bool numeric(Identifier id) {
  for (std::size_t i = 0; i < id.size; ++i) {
    if (id.data[i] < '0' || id.data[i] > '9')
      return false;
  }
  return id.size != 0;
}

inline bool parse_version(const char* text, Version& version) {
  if (text == nullptr)
    return false;
  Version parsed{};
  const char* cursor = text;
  for (auto& part : parsed.core) {
    const char* start = cursor;
    while (*cursor >= '0' && *cursor <= '9')
      ++cursor;
    part = {start, static_cast<std::size_t>(cursor - start)};
    if (part.size == 0 || (part.size > 1 && *start == '0'))
      return false;
    if (&part != &parsed.core[2] && *cursor++ != '.')
      return false;
  }
  if (*cursor == '-') {
    const char* start = ++cursor;
    const char* identifier = cursor;
    while (*cursor != 0 && *cursor != '+') {
      const char c = *cursor;
      if (c == '.') {
        if (cursor == identifier ||
            (numeric({identifier, static_cast<std::size_t>(cursor - identifier)}) &&
             cursor - identifier > 1 && *identifier == '0'))
          return false;
        identifier = cursor + 1;
      } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   c == '-')) {
        return false;
      }
      ++cursor;
    }
    if (cursor == identifier ||
        (numeric({identifier, static_cast<std::size_t>(cursor - identifier)}) &&
         cursor - identifier > 1 && *identifier == '0'))
      return false;
    parsed.prerelease = {start, static_cast<std::size_t>(cursor - start)};
  }
  if (*cursor == '+') {
    ++cursor;
    const char* identifier = cursor;
    while (*cursor != 0) {
      const char c = *cursor;
      if (c == '.') {
        if (cursor == identifier)
          return false;
        identifier = cursor + 1;
      } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   c == '-')) {
        return false;
      }
      ++cursor;
    }
    if (cursor == identifier)
      return false;
  }
  if (*cursor != 0)
    return false;
  version = parsed;
  return true;
}

inline int compare_identifier(Identifier a, Identifier b, bool prerelease = false) {
  const bool a_numeric = numeric(a);
  const bool b_numeric = numeric(b);
  if (prerelease && a_numeric != b_numeric)
    return a_numeric ? -1 : 1;
  if ((!prerelease || (a_numeric && b_numeric)) && a.size != b.size)
    return a.size < b.size ? -1 : 1;
  const std::size_t length = a.size < b.size ? a.size : b.size;
  const int comparison = std::memcmp(a.data, b.data, length);
  if (comparison != 0)
    return comparison < 0 ? -1 : 1;
  if (a.size != b.size)
    return a.size < b.size ? -1 : 1;
  return 0;
}

inline int compare_versions(const Version& candidate, const Version& running) {
  for (std::size_t i = 0; i < 3; ++i) {
    const int result = compare_identifier(candidate.core[i], running.core[i]);
    if (result != 0)
      return result;
  }
  if (candidate.prerelease.size == 0 || running.prerelease.size == 0) {
    if (candidate.prerelease.size == running.prerelease.size)
      return 0;
    return candidate.prerelease.size == 0 ? 1 : -1;
  }
  const char* a = candidate.prerelease.data;
  const char* b = running.prerelease.data;
  const char* a_end = a + candidate.prerelease.size;
  const char* b_end = b + running.prerelease.size;
  while (a < a_end && b < b_end) {
    const char* a_next = a;
    const char* b_next = b;
    while (a_next < a_end && *a_next != '.')
      ++a_next;
    while (b_next < b_end && *b_next != '.')
      ++b_next;
    const int result = compare_identifier({a, static_cast<std::size_t>(a_next - a)},
                                          {b, static_cast<std::size_t>(b_next - b)}, true);
    if (result != 0)
      return result;
    a = a_next == a_end ? a_end : a_next + 1;
    b = b_next == b_end ? b_end : b_next + 1;
  }
  if (a == a_end && b == b_end)
    return 0;
  return a == a_end ? -1 : 1;
}

inline bool newer_version(const char* candidate, const char* running) {
  Version proposed{};
  Version current{};
  return parse_version(candidate, proposed) && parse_version(running, current) &&
         compare_versions(proposed, current) > 0;
}

inline bool digest_equal(const std::uint8_t* left, const std::uint8_t* right, std::size_t size) {
  if (left == nullptr || right == nullptr)
    return false;
  std::uint8_t difference = 0;
  for (std::size_t index = 0; index < size; ++index)
    difference |= static_cast<std::uint8_t>(left[index] ^ right[index]);
  return difference == 0;
}

inline bool session_matches(const std::uint8_t* requested, const std::uint8_t* active,
                            std::size_t size) {
  if (!requested || !active || size == 0)
    return false;
  std::uint8_t difference = 0;
  std::uint8_t present = 0;
  for (std::size_t i = 0; i < size; ++i) {
    difference |= static_cast<std::uint8_t>(requested[i] ^ active[i]);
    present |= active[i];
  }
  return difference == 0 && present != 0;
}

constexpr ChunkError validate_chunk(const std::uint8_t* data, std::size_t chunk_size,
                                    std::size_t maximum_chunk_size, std::uint32_t offset,
                                    std::uint32_t written, std::uint32_t total, bool final_chunk) {
  if (data == nullptr || chunk_size == 0)
    return ChunkError::invalid_argument;
  if (chunk_size > maximum_chunk_size)
    return ChunkError::chunk_too_large;
  if (offset != written)
    return ChunkError::offset_mismatch;
  if (written > total || chunk_size > total - written)
    return ChunkError::image_too_large;
  const std::uint32_t next = written + static_cast<std::uint32_t>(chunk_size);
  if (final_chunk != (next == total))
    return ChunkError::final_mismatch;
  return ChunkError::ok;
}

constexpr std::uint8_t progress_percent(std::uint32_t written, std::uint32_t total) {
  return total == 0
             ? 0
             : static_cast<std::uint8_t>((static_cast<std::uint64_t>(written) * 100U) / total);
}

} // namespace core_pilot::ota_policy
