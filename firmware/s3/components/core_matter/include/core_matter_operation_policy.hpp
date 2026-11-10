#pragma once

#include <cstddef>
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

struct InvokeToken final {
  std::size_t slot{};
  std::uint32_t generation{};
};

[[nodiscard]] constexpr std::uintptr_t encodeInvokeToken(std::size_t slot,
                                                         std::uint32_t generation) {
  return generation == 0 ? 0 : (static_cast<std::uintptr_t>(generation) << 8U) | (slot + 1U);
}

[[nodiscard]] constexpr InvokeToken decodeInvokeToken(std::uintptr_t token) {
  const std::size_t encoded_slot = token & 0xffU;
  return {encoded_slot == 0 ? 0 : encoded_slot - 1U, static_cast<std::uint32_t>(token >> 8U)};
}

[[nodiscard]] constexpr bool invokeTokenMatches(std::uintptr_t token, std::size_t capacity,
                                                std::size_t slot, std::uint32_t generation) {
  const InvokeToken decoded = decodeInvokeToken(token);
  return token != 0 && decoded.slot < capacity && decoded.slot == slot &&
         decoded.generation == generation;
}

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
