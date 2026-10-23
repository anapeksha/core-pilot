#pragma once

#include "core_security.hpp"
#include "esp_err.h"

#include <cstddef>
#include <cstdint>

namespace core_pilot {

struct ThreadDataset final {
  static constexpr std::size_t kMaxBytes = 254;

  std::uint8_t tlvs[kMaxBytes]{};
  std::uint8_t length{0};
  std::uint64_t generation{0};

  void clear();
};

bool decodeThreadDatasetHex(const char* hex, ThreadDataset& dataset);
bool validateThreadDataset(const std::uint8_t* tlvs, std::size_t length);
core_security::Status loadThreadDataset(core_security::StorageService& storage,
                                        ThreadDataset& dataset);
core_security::Status saveThreadDataset(core_security::StorageService& storage,
                                        core_security::RandomSource& random,
                                        ThreadDataset& dataset);

} // namespace core_pilot
