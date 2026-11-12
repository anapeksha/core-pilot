#pragma once

#include "core_domain.hpp"

#include <cstdint>

namespace core_domain {

class HttpConnectionPolicy final {
public:
  static constexpr std::uint8_t max_per_source = 2;

  bool admit(int socket, std::uint64_t source) {
    std::uint8_t source_count = 0;
    Entry* free = nullptr;
    for (auto& entry : entries_) {
      if (entry.used && entry.source == source)
        ++source_count;
      if (!entry.used && free == nullptr)
        free = &entry;
    }
    if (free == nullptr || source_count >= max_per_source)
      return false;
    *free = {.socket = socket, .source = source, .used = true};
    return true;
  }

  bool release(int socket) {
    for (auto& entry : entries_) {
      if (entry.used && entry.socket == socket) {
        entry = {};
        return true;
      }
    }
    return false;
  }

private:
  struct Entry final {
    int socket{-1};
    std::uint64_t source{};
    bool used{};
  };
  Entry entries_[max_http_connections]{};
};

} // namespace core_domain
