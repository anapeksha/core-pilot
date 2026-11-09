#pragma once

#include <cstdint>

namespace core_pilot {

enum class CommissioningFailureClass : std::uint8_t {
  none,
  pase,
  attestation,
  credentials,
  network,
  operational,
  commissioning,
};

inline constexpr std::uint8_t kUnknownCommissioningStage = 0xff;

namespace detail {

[[nodiscard]] constexpr bool unpairCallbackSlotAvailable(std::uint32_t operation_id) {
  return operation_id == 0;
}

[[nodiscard]] constexpr bool unpairCallbackMatches(std::uint32_t operation_id,
                                                   std::uint64_t expected_node_id,
                                                   std::uint32_t generation,
                                                   std::uint64_t callback_node_id) {
  return operation_id != 0 && generation != 0 && expected_node_id == callback_node_id;
}

} // namespace detail
} // namespace core_pilot
